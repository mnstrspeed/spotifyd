/*
 * spotifyd - A daemon playing music from spotify, in the spirit of MPD.
 * Copyright (C) 2015 Simon Persson
 * 
 * Spotifyd program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Spotifyd program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <libspotify/api.h>
#include <stdio.h>
#include <string.h>

#include "socket.h"
#include "queue.h"
#include "helpers.h"
#include "playlist.h"
#include "search.h"

void command_search(sp_session *session, const struct command * const command)
{
	if(command->search_string == NULL)
	{
		LOG_PRINT("execute_command: search_string is null-ptr.\n");
	}
	else
	{
		sp_search_create(session, 
			command->search_string,
			0,
			NUM_SEARCH_RESULTS,
			0,
			NUM_SEARCH_RESULTS,
			0,
			NUM_SEARCH_RESULTS,
			0,
			NUM_SEARCH_RESULTS,
			SP_SEARCH_STANDARD,
			&on_search_complete,
			session
		);
	}
}

void command_link(sp_session *session, const struct command * const command)
{
	sp_link *l = sp_link_create_from_string(command->search_string);

	if(l == NULL || sp_link_type(l) == SP_LINKTYPE_INVALID)
	{
		sock_send_str(command->sockfd, "Not a valid link.\n");
		return;
	}
	else if(sp_link_type(l) == SP_LINKTYPE_TRACK)
	{
		sp_track *t = sp_link_as_track(l);
		queue_add_track(t);
		sock_send_str(command->sockfd, "Added track to queue.\n");
	}
	else if (sp_link_type(l) == SP_LINKTYPE_ALBUM)
	{
		sp_album *a = sp_link_as_album(l);
		queue_add_album(session, a);

		sock_send_str(command->sockfd, "Adding album ");
		sock_send_str(command->sockfd, sp_album_name(a));
		sock_send_str(command->sockfd, " by ");
		sock_send_str(command->sockfd, sp_artist_name(sp_album_artist(a)));
		sock_send_str(command->sockfd, " to queue.\n");
	}
	else if (sp_link_type(l) == SP_LINKTYPE_PLAYLIST)
	{
		sp_playlist *pl = sp_playlist_create(session, l);
		queue_add_playlist(pl);
		sock_send_str(command->sockfd, "Adding playlist.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Link is valid but its type is not supported. Only links to tracks are supported.\n");
	}

	sp_link_release(l);
}

void command_qclear(sp_session *session)
{
	sp_session_player_play(session, 0);
	sp_session_player_unload(session);
	queue_clear();
}

void command_qrand(sp_session *session, const struct command * const command)
{
	queue_shuffle();
	sock_send_str(command->sockfd, "Shuffled queue.\n");
}

void command_qrm(sp_session *session, const struct command * const command)
{
	if(queue_del_track(command->track))
	{
		sock_send_str(command->sockfd, "Removing from queue.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Track not in queue!\n");
	}
}

/*
 * Send a list of the search result to the client.
 */
void command_lists(sp_session *session, const struct command * const command)
{
	sock_send_str(command->sockfd, search_str_list());
}

/*
 * Send a list of the queue to the client.
 */
void command_listq(sp_session *session, const struct command * const command)
{
	unsigned i = 0;
	if(queue_print_cur_first)
	{
		i = queue_get_pos();
	}

	while(queue_get(i) != NULL && i < NUM_SEARCH_RESULTS)
	{
		char buf[API_MESSAGE_LEN];
		snprintf(buf, API_MESSAGE_LEN, "%d | ", i);
		track_to_str(buf + strlen(buf), API_MESSAGE_LEN - strlen(buf), queue_get(i));
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, "\n");
		++i;
	}
}

void command_qprint(const struct command * const command)
{
	queue_print_cur_first = !queue_print_cur_first;
	if(queue_print_cur_first)
	{
		sock_send_str(command->sockfd, "Will print the currently playing song first.\n");
	}	
	else
	{
		sock_send_str(command->sockfd, "Will print the first song in queue first.\n");
	}
}

void command_qadd(sp_session *session, const struct command * const command)
{
	char buf[API_MESSAGE_LEN];
	buf[0] = '\0';
	search_for_tracks_at(session, buf, API_MESSAGE_LEN, command->track, queue_add_track);
	if(strlen(buf) != 0)
	{
		sock_send_str(command->sockfd, "Adding: ");
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, "\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Couldn't add that!\n");
	}
}

void command_cur_playing(const struct command * const command)
{
	sp_track *t;
	if((t = queue_get_current()) == NULL)
	{
		sock_send_str(command->sockfd, "Not playing a track right now.\n");
	}
	else
	{
		char buf[API_MESSAGE_LEN];
		track_to_str(buf, API_MESSAGE_LEN, t);
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, "\n");
	}
}
	

void command_play(sp_session *session, const struct command * const command)
{
	if(command->track < queue_get_len())
	{
		queue_set_current(command->track);

		/*
		 * If the track fails to play, we try to play the next song.
		 * If no track is found within the maximum length of the queue,
		 * give up.
		 */
		int cntr = 0;
		int track = command->track;
		while(!play(session, queue_get(track), 1))
		{
			queue_del_track(track);	
			++cntr;
			if(cntr == PLAY_QUEUE_LEN)
			{
				sock_send_str(command->sockfd, "There doesn't seem to be any playable track in the queue.\n");
				return;
			}
			track = queue_get_next();
		}
		queue_set_current(track);
		char buf[API_MESSAGE_LEN];
		track_to_str(buf, API_MESSAGE_LEN, queue_get(track));
		sock_send_str(command->sockfd, "Playing: ");
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, "\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Track not in queue!\n");
	}
}

void command_prev(sp_session *session, struct command * const command)
{
	command->type = PLAY;
	command->track = queue_get_prev();
	command_play(session, command);
}

void command_next(sp_session *session, struct command * const command)
{
	command->type = PLAY;
	command->track = queue_get_next();
	command_play(session, command);
}

void command_pause(sp_session *session, const struct command * const command)
{
	sp_session_player_play(session, is_playing =! is_playing);
	if(is_playing)
	{
		sock_send_str(command->sockfd, "Started playback.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Paused playback.\n");
	}
}

void command_pl(const struct command * const command)
{
	unsigned i = 0;
	for(i = 0; i<playlist_len(); ++i)
	{
		char playlist_name[API_MESSAGE_LEN];
		playlist_get_name(playlist_name, API_MESSAGE_LEN, i);
		if(playlist_name == NULL)
		{
			break;
		}
		char name_str[API_MESSAGE_LEN];
		snprintf(name_str, API_MESSAGE_LEN, "%d | %s\n", i, playlist_name);
		sock_send_str(command->sockfd, name_str);
	}
}

void command_pladd(sp_session *session, const struct command * const command)
{
	sp_track *t = queue_get(command->track);
	if(t != NULL && playlist_add_track(command->playlist, t, session))
	{
		sock_send_str(command->sockfd, "Added track ");
		char buf[API_MESSAGE_LEN];
		track_to_str(buf, API_MESSAGE_LEN, queue_get(command->track));
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, " to playlist ");
		playlist_get_name(buf, API_MESSAGE_LEN, command->playlist);
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, ".\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Couldn't add track.\n");
	}

}

void command_plrm(const struct command * const command)
{
	if(playlist_del_track(command->playlist, command->track))
	{
		sock_send_str(command->sockfd, "Removed track ");
		char buf[API_MESSAGE_LEN];
		track_to_str(buf, API_MESSAGE_LEN, queue_get(command->track));
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, " from playlist ");
		playlist_get_name(buf, API_MESSAGE_LEN, command->playlist);
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, ".\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Couldn't remove track.\n");
	}

}

void command_plcreate(const struct command * const command)
{
	if(playlist_new(command->name))
	{
		sock_send_str(command->sockfd, "Created new playlist.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Couldn't create new playlist.\n");
	}
}

void command_pldelete(const struct command * const command)
{
	if(playlist_remove(command->playlist))
	{
		sock_send_str(command->sockfd, "Removed playlist.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "Couldn't remove playlist.\n");
	}
}

void command_qaddpl(const struct command * const command)
{	
	while(queue_get_len() != 0) queue_del_track(0);
	if(playlist_for_each(command->playlist, &queue_add_track))
	{
		sock_send_str(command->sockfd, "Added playlist \"");
		char buf[API_MESSAGE_LEN];
		playlist_get_name(buf, API_MESSAGE_LEN, command->playlist);
		sock_send_str(command->sockfd, buf);
		sock_send_str(command->sockfd, "\" to queue.\n");
	}
	else
	{
		sock_send_str(command->sockfd, "No such playlist.\n");
	}
}

void command_vol(const struct command * const command)
{
	if(command->volume > 100)
	{
		sock_send_str(command->sockfd, "Error: volume must be in the range 0 to 100.\n");
	}
	else
	{
		set_volume(command->volume / 100.0);
	}
}
