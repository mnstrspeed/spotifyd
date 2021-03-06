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
#include <stdio.h>
#include <libspotify/api.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio.h"
#include "queue.h"
#include "socket.h"
#include "config.h"
#include "helpers.h"
#include "spotifyd.h"
#include "audio.h"
#include "playlist.h"
#include "search.h"

/*
 * from jukebox.c in the libspotify examples. Thanks spotify <3
 *
 * see audio.c for full license of this method.
 */
int on_music_delivered(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames)
{
	debug("on_music_delivered\n");

	audio_fifo_t *af = &g_audiofifo;
	audio_fifo_data_t *afd;
	size_t s;

	if (num_frames == 0)
		return 0; // Audio discontinuity, do nothing
	
	pthread_mutex_lock(&af->mutex);

	/* Buffer one second of audio */
	if (af->qlen > format->sample_rate) {
		pthread_mutex_unlock(&af->mutex);
		return 0;
	}

	s = num_frames * sizeof(int16_t) * format->channels;

	afd = malloc(sizeof(*afd) + s);
	if(afd == NULL)
	{
		LOG_PRINT("Can't allocate memory. Quitting.\n");
		exit(1);
	}

	memcpy(afd->samples, frames, s);

	afd->nsamples = num_frames;

	afd->rate = format->sample_rate;
	afd->channels = format->channels;

	TAILQ_INSERT_TAIL(&af->q, afd, link);
	af->qlen += num_frames;

	pthread_cond_signal(&af->cond);
	pthread_mutex_unlock(&af->mutex);

	return num_frames;
}

void on_notify_main_thread(sp_session *sess)
{
	debug("on_notify_main_thread\n");
	notify_main_thread();
}

void on_end_of_track(sp_session *session)
{
	debug("on_end_of_track\n");
	sp_session_player_unload(session);
	/*
	 * Add a play command containing next song to be played
	 * to the command queue.
	 */
	struct commandq_entry *entry = malloc(sizeof(struct commandq_entry));
	struct command *command = malloc(sizeof(struct command));
	if(command == NULL || entry == NULL)
	{
		LOG_PRINT("Can't allocate memory. Quitting.\n");
		exit(1);
	}
	entry->val = command;
	command->type = NEXT;
	command->done = 0;
	command->handled = 0;
	commandq_insert(entry);
	pthread_mutex_lock(&commandq_lock);
	notify_main_thread();
	pthread_mutex_unlock(&commandq_lock);
}

void on_search_complete(sp_search *search, void *userdata)
{
	debug("on_search_complete\n");
	
	sp_error error = sp_search_error(search);
	if (error != SP_ERROR_OK)
	{
		LOG_PRINT("Error: %s\n", sp_error_message(error));
		exit(1);
	}

	pthread_mutex_lock(&commandq_lock);
	int sockfd = commandq.tqh_first->val->sockfd;
	
	search_new_search(search);
	char *search_list = search_str_list();
	sock_send_str(sockfd, search_list);
	free(search_list);
	
	/*
	 * If we ended up here, that means that the first element on the
	 * commandq is a search. Set it to done and notify the main thread 
	 * so the search command can be freed.
	 */
	close(sockfd);
	commandq.tqh_first->val->done = 1;
	pthread_mutex_unlock(&commandq_lock);
	notify_main_thread();

	sp_search_release(search);
}

void on_albumbrowse_complete(sp_albumbrowse *result, void *userdata)
{
	bool (*f)(sp_track *) = (bool (*)(sp_track *)) userdata;
	int i;
	for(i=0; i<sp_albumbrowse_num_tracks(result); ++i)
	{
		f(sp_albumbrowse_track(result, i));
	}

	sp_albumbrowse_release(result);
}

void on_login(sp_session *session, sp_error error)
{
	debug("on_login\n");
	if(error != SP_ERROR_OK)
	{
		LOG_PRINT("Couldn't log in: %s\n", sp_error_message(error));
		exit (1);
	}
	else
	{
		LOG_PRINT("Logged in!\n");
	}

	playlist_init(session);
	notify_main_thread();
}
