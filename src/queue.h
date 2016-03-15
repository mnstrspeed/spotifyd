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
#pragma once

#include <libspotify/api.h>

/*
 * When printing the queue, which song should come first?
 * Alternatives are: The currently playing song and the first song in queue.
 */
bool queue_print_cur_first;

int queue_get_next();
int queue_get_prev();
unsigned queue_get_len();
sp_track *queue_get(unsigned);
void queue_set_current(unsigned);
int queue_get_pos();
sp_track *queue_get_current();
bool queue_del_track(unsigned trackn);
void queue_shuffle();
bool queue_add_track(sp_track *track);
bool queue_add_album(sp_session *session, sp_album *album);
bool queue_add_playlist(sp_playlist *playlist);
void queue_init();
void queue_clear();
