/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2014-2017 - Jean-André Santoni
 *  Copyright (C) 2015-2018 - Andre Leiradella
 *  Copyright (C) 2018-2020 - natinusala
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <retro_miscellaneous.h>
#include <retro_inline.h>

#include <lists/file_list.h>
#include <queues/fifo_queue.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
#include <string/stdstring.h>
#include <retro_math.h>

#ifdef HAVE_THREADS
#include <rthreads/rthreads.h>
#define SLOCK_LOCK(x) slock_lock(x)
#define SLOCK_UNLOCK(x) slock_unlock(x)
#else
#define SLOCK_LOCK(x)
#define SLOCK_UNLOCK(x)
#endif

#include "gfx_widgets.h"

#include "gfx_display.h"
#include "font_driver.h"

#include "../msg_hash.h"

#include "../tasks/task_content.h"

#ifdef HAVE_CHEEVOS
#include "../cheevos/badges.h"
#endif

#define BASE_FONT_SIZE 32.0f

#define MSG_QUEUE_FONT_SIZE (BASE_FONT_SIZE * 0.69f)

#ifdef HAVE_CHEEVOS
#define CHEEVO_QUEUE_SIZE 8
#endif

#ifdef HAVE_MENU
#define ANIMATION_LOAD_CONTENT_DURATION            333

#define LOAD_CONTENT_ANIMATION_INITIAL_ICON_SIZE   320
#define LOAD_CONTENT_ANIMATION_TARGET_ICON_SIZE    240
#endif

enum gfx_widgets_icon
{
   MENU_WIDGETS_ICON_PAUSED = 0,
   MENU_WIDGETS_ICON_FAST_FORWARD,
   MENU_WIDGETS_ICON_REWIND,
   MENU_WIDGETS_ICON_SLOW_MOTION,

   MENU_WIDGETS_ICON_HOURGLASS,
   MENU_WIDGETS_ICON_CHECK,

   MENU_WIDGETS_ICON_INFO,

   MENU_WIDGETS_ICON_ACHIEVEMENT,

   MENU_WIDGETS_ICON_LAST
};

/* Font data */
typedef struct
{
   gfx_widget_font_data_t regular;
   gfx_widget_font_data_t bold;
   gfx_widget_font_data_t msg_queue;
} gfx_widget_fonts_t;

#ifdef HAVE_CHEEVOS
typedef struct cheevo_popup
{
   char* title;
   uintptr_t badge;
} cheevo_popup;
#endif

typedef struct menu_widget_msg
{
   char *msg;
   char *msg_new;
   float msg_transition_animation;
   unsigned msg_len;
   unsigned duration;

   unsigned text_height;

   float offset_y;
   float alpha;

   /* Is it currently doing the fade out animation ? */
   bool dying;
   /* Has the timer expired ? if so, should be set to dying */
   bool expired;
   unsigned width;

   gfx_timer_t expiration_timer;
   bool expiration_timer_started;

   retro_task_t *task_ptr;
   /* Used to detect title change */
   char *task_title_ptr;
   /* How many tasks have used this notification? */
   uint8_t task_count;

   int8_t task_progress;
   bool task_finished;
   bool task_error;
   bool task_cancelled;
   uint32_t task_ident;

   /* Unfold animation */
   bool unfolded;
   bool unfolding;
   float unfold;

   float hourglass_rotation;
   gfx_timer_t hourglass_timer;
} menu_widget_msg_t;

typedef struct dispgfx_widget
{
   bool widgets_active;
   /* There can only be one message animation at a time to 
    * avoid confusing users */
   bool widgets_moving;
   bool widgets_inited;
   bool widgets_persisting;
   bool msg_queue_has_icons;
#ifdef HAVE_MENU
   bool load_content_animation_running;
#endif

#ifdef HAVE_CHEEVOS
   int cheevo_popup_queue_read_index;
#endif
#ifdef HAVE_TRANSLATE
   int ai_service_overlay_state;
#endif
#ifdef HAVE_CHEEVOS
   int cheevo_popup_queue_write_index;
#endif
#ifdef HAVE_CHEEVOS
   float cheevo_unfold;
   float cheevo_y;
#endif
#ifdef HAVE_MENU
   float load_content_animation_icon_color[16];
   float load_content_animation_icon_size;
   float load_content_animation_icon_alpha;
   float load_content_animation_fade_alpha;
   float load_content_animation_final_fade_alpha;
#endif
   float last_scale_factor;
#ifdef HAVE_MENU
   unsigned load_content_animation_icon_size_initial;
   unsigned load_content_animation_icon_size_target;
#endif
#ifdef HAVE_TRANSLATE
   unsigned ai_service_overlay_width;
   unsigned ai_service_overlay_height;
#endif
   unsigned last_video_width;
   unsigned last_video_height;
   unsigned msg_queue_kill;
   /* Count of messages bound to a task in current_msgs */
   unsigned msg_queue_tasks_count;
#ifdef HAVE_CHEEVOS
   unsigned cheevo_width;
   unsigned cheevo_height;
#endif
   unsigned simple_widget_padding;
   unsigned simple_widget_height;

   /* Used for both generic and libretro messages */
   unsigned generic_message_height;

   unsigned msg_queue_height;
   unsigned msg_queue_spacing;
   unsigned msg_queue_rect_start_x;
   unsigned msg_queue_internal_icon_size;
   unsigned msg_queue_internal_icon_offset;
   unsigned msg_queue_icon_size_x;
   unsigned msg_queue_icon_size_y;
   unsigned msg_queue_icon_offset_y;
   unsigned msg_queue_scissor_start_x;
   unsigned msg_queue_default_rect_width_menu_alive;
   unsigned msg_queue_default_rect_width;
   unsigned msg_queue_regular_padding_x;
   unsigned msg_queue_regular_text_start;
   unsigned msg_queue_task_text_start_x;
   unsigned msg_queue_task_rect_start_x;
   unsigned msg_queue_task_hourglass_x;
   unsigned divider_width_1px;

   uint64_t gfx_widgets_frame_count;

#ifdef HAVE_MENU
   uintptr_t load_content_animation_icon;
#endif
#ifdef HAVE_TRANSLATE
   uintptr_t ai_service_overlay_texture;
#endif
   uintptr_t msg_queue_icon;
   uintptr_t msg_queue_icon_outline;
   uintptr_t msg_queue_icon_rect;
   uintptr_t gfx_widgets_icons_textures[
   MENU_WIDGETS_ICON_LAST];

   char gfx_widgets_fps_text[255];
#ifdef HAVE_MENU
   char *load_content_animation_content_name;
   char *load_content_animation_playlist_name;
#endif
#ifdef HAVE_MENU
   gfx_timer_t load_content_animation_end_timer;
#endif
   uintptr_t gfx_widgets_generic_tag;
   gfx_widget_fonts_t gfx_widget_fonts;
#ifdef HAVE_CHEEVOS
/* Achievement notification */
   cheevo_popup cheevo_popup_queue[CHEEVO_QUEUE_SIZE];
   gfx_timer_t cheevo_timer;
#endif
   fifo_buffer_t *msg_queue;
   file_list_t *current_msgs;
#if defined(HAVE_CHEEVOS) && defined(HAVE_THREADS)
   slock_t* cheevo_popup_queue_lock;
#endif
} dispgfx_widget_t;


/* TODO/FIXME - global state - perhaps move outside this file */
static float msg_queue_background[16]                    =
COLOR_HEX_TO_FLOAT(0x3A3A3A, 1.0f);
static float msg_queue_info[16]                          =
COLOR_HEX_TO_FLOAT(0x12ACF8, 1.0f);

/* Color of first progress bar in a task message */
static float msg_queue_task_progress_1[16]               = 
COLOR_HEX_TO_FLOAT(0x397869, 1.0f);

/* Color of second progress bar in a task message 
 * (for multiple tasks with same message) */
static float msg_queue_task_progress_2[16]               = 
COLOR_HEX_TO_FLOAT(0x317198, 1.0f);

#if 0
static float color_task_progress_bar[16]                 = 
COLOR_HEX_TO_FLOAT(0x22B14C, 1.0f);
#endif
static float gfx_widgets_pure_white[16]                  = {
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
      1.00, 1.00, 1.00, 1.00,
};

static float gfx_widgets_backdrop_orig[16]               = {
   0.00, 0.00, 0.00, 0.75,
   0.00, 0.00, 0.00, 0.75,
   0.00, 0.00, 0.00, 0.75,
   0.00, 0.00, 0.00, 0.75,
};

static float gfx_widgets_backdrop[16]                    = {
      0.00, 0.00, 0.00, 0.75,
      0.00, 0.00, 0.00, 0.75,
      0.00, 0.00, 0.00, 0.75,
      0.00, 0.00, 0.00, 0.75,
};

/* Icons */
static const char 
*gfx_widgets_icons_names[MENU_WIDGETS_ICON_LAST]         = {
   "menu_pause.png",
   "menu_frameskip.png",
   "menu_rewind.png",
   "resume.png",

   "menu_hourglass.png",
   "menu_check.png",

   "menu_info.png",

   "menu_achievements.png"
};

/* Forward declarations */
#ifdef HAVE_CHEEVOS
static void gfx_widgets_start_achievement_notification(
     dispgfx_widget_t *p_dispwidget);
#endif
#ifdef HAVE_MENU
bool menu_driver_get_load_content_animation_data(
      uintptr_t *icon, char **playlist_name);
#endif
static void gfx_widgets_context_reset(
      dispgfx_widget_t *p_dispwidget,
      bool is_threaded,
      unsigned width, unsigned height, bool fullscreen,
      const char *dir_assets, char *font_path);
static void gfx_widgets_context_destroy(dispgfx_widget_t *p_dispwidget);
static void gfx_widgets_free(dispgfx_widget_t *p_dispwidget);
static void gfx_widgets_layout(dispgfx_widget_t *p_dispwidget,
      bool is_threaded, const char *dir_assets, char *font_path);

void *dispwidget_get_ptr(void)
{
   /* TODO/FIXME - static global state - perhaps move outside this file */
   static dispgfx_widget_t dispwidget_st;
   return &dispwidget_st;
}

bool gfx_widgets_active(void)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   return p_dispwidget->widgets_active;
}

void gfx_widgets_set_persistence(bool persist)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   p_dispwidget->widgets_persisting = persist;
}

gfx_widget_font_data_t* gfx_widgets_get_font_regular(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return &p_dispwidget->gfx_widget_fonts.regular;
}

gfx_widget_font_data_t* gfx_widgets_get_font_bold(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return &p_dispwidget->gfx_widget_fonts.bold;
}

gfx_widget_font_data_t* gfx_widgets_get_font_msg_queue(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return &p_dispwidget->gfx_widget_fonts.msg_queue;
}

float* gfx_widgets_get_pure_white(void)
{
   return gfx_widgets_pure_white;
}

float* gfx_widgets_get_backdrop_orig(void)
{
   return gfx_widgets_backdrop_orig;
}

/* Messages queue */

uintptr_t gfx_widgets_get_generic_tag(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->gfx_widgets_generic_tag;
}

unsigned gfx_widgets_get_padding(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->simple_widget_padding;
}

unsigned gfx_widgets_get_height(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->simple_widget_height;
}

unsigned gfx_widgets_get_generic_message_height(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->generic_message_height;
}

unsigned gfx_widgets_get_last_video_width(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->last_video_width;
}

unsigned gfx_widgets_get_last_video_height(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->last_video_height;
}

size_t gfx_widgets_get_msg_queue_size(void *data)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)data;
   return p_dispwidget->current_msgs ? p_dispwidget->current_msgs->size : 0;
}

/* Widgets list */
const static gfx_widget_t* const widgets[] = {
   &gfx_widget_screenshot,
   &gfx_widget_volume,
   &gfx_widget_generic_message,
   &gfx_widget_libretro_message
};

static void msg_widget_msg_transition_animation_done(void *userdata)
{
   menu_widget_msg_t *msg = (menu_widget_msg_t*)userdata;

   if (msg->msg)
      free(msg->msg);
   msg->msg = NULL;

   if (msg->msg_new)
      msg->msg = strdup(msg->msg_new);

   msg->msg_transition_animation = 0.0f;
}

void gfx_widgets_msg_queue_push(
      retro_task_t *task, const char *msg,
      unsigned duration,
      char *title,
      enum message_queue_icon icon,
      enum message_queue_category category,
      unsigned prio, bool flush,
      bool menu_is_alive)
{
   menu_widget_msg_t    *msg_widget = NULL;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (!p_dispwidget->widgets_active)
      return;

   if (fifo_write_avail(p_dispwidget->msg_queue) > 0)
   {
      /* Get current msg if it exists */
      if (task && task->frontend_userdata)
      {
         msg_widget           = (menu_widget_msg_t*)task->frontend_userdata;
         /* msg_widgets can be passed between tasks */
         msg_widget->task_ptr = task;
      }

      /* Spawn a new notification */
      if (!msg_widget)
      {
         const char *title                      = msg;

         msg_widget                             = (menu_widget_msg_t*)calloc(1, sizeof(*msg_widget));

         if (task)
            title                               = task->title;

         msg_widget->duration                   = duration;
         msg_widget->offset_y                   = 0;
         msg_widget->alpha                      = 1.0f;

         msg_widget->dying                      = false;
         msg_widget->expired                    = false;

         msg_widget->expiration_timer           = 0;
         msg_widget->task_ptr                   = task;
         msg_widget->expiration_timer_started   = false;

         msg_widget->msg_new                    = NULL;
         msg_widget->msg_transition_animation   = 0.0f;

         msg_widget->text_height                = 0;

         if (p_dispwidget->msg_queue_has_icons)
         {
            msg_widget->unfolded                = false;
            msg_widget->unfolding               = false;
            msg_widget->unfold                  = 0.0f;
         }
         else
         {
            msg_widget->unfolded                = true;
            msg_widget->unfolding               = false;
            msg_widget->unfold                  = 1.0f;
         }

         if (task)
         {
            msg_widget->msg                  = strdup(title);
            msg_widget->msg_new              = strdup(title);
            msg_widget->msg_len              = (unsigned)strlen(title);

            msg_widget->task_error           = !string_is_empty(task->error);
            msg_widget->task_cancelled       = task->cancelled;
            msg_widget->task_finished        = task->finished;
            msg_widget->task_progress        = task->progress;
            msg_widget->task_ident           = task->ident;
            msg_widget->task_title_ptr       = task->title;
            msg_widget->task_count           = 1;

            msg_widget->unfolded             = true;

            msg_widget->width                = font_driver_get_message_width(
                  p_dispwidget->gfx_widget_fonts.msg_queue.font,
                  title,
                  msg_widget->msg_len, 1) +
                  p_dispwidget->simple_widget_padding / 2;

            task->frontend_userdata          = msg_widget;

            msg_widget->hourglass_rotation   = 0;
         }
         else
         {
            /* Compute rect width, wrap if necessary */
            /* Single line text > two lines text > two lines 
             * text with expanded width */
            unsigned title_length   = (unsigned)strlen(title);
            char *msg               = strdup(title);
            unsigned width          = menu_is_alive 
               ? p_dispwidget->msg_queue_default_rect_width_menu_alive 
               : p_dispwidget->msg_queue_default_rect_width;
            unsigned text_width     = font_driver_get_message_width(
                  p_dispwidget->gfx_widget_fonts.msg_queue.font,
                  title,
                  title_length,
                  1);
            msg_widget->text_height = p_dispwidget->gfx_widget_fonts.msg_queue.line_height;

            /* Text is too wide, split it into two lines */
            if (text_width > width)
            {
               /* If the second line is too short, the widget may
                * look unappealing - ensure that second line is at
                * least 25% of the total width */
               if ((text_width - (text_width >> 2)) < width)
                  width = text_width - (text_width >> 2);

               word_wrap(msg, msg, (title_length * width) / text_width,
                     false, 2);

               msg_widget->text_height *= 2;
            }
            else
               width = text_width;

            msg_widget->msg         = msg;
            msg_widget->msg_len     = (unsigned)strlen(msg);
            msg_widget->width       = width + 
               p_dispwidget->simple_widget_padding / 2;
         }

         fifo_write(p_dispwidget->msg_queue, &msg_widget, sizeof(msg_widget));
      }
      /* Update task info */
      else
      {
         if (msg_widget->expiration_timer_started)
         {
            gfx_timer_kill(&msg_widget->expiration_timer);
            msg_widget->expiration_timer_started = false;
         }

         if (!string_is_equal(task->title, msg_widget->msg_new))
         {
            unsigned len         = (unsigned)strlen(task->title);
            unsigned new_width   = font_driver_get_message_width(
                  p_dispwidget->gfx_widget_fonts.msg_queue.font,
                  task->title,
                  len,
                  1);

            if (msg_widget->msg_new)
            {
               free(msg_widget->msg_new);
               msg_widget->msg_new                 = NULL;
            }

            msg_widget->msg_new                    = strdup(task->title);
            msg_widget->msg_len                    = len;
            msg_widget->task_title_ptr             = task->title;
            msg_widget->msg_transition_animation   = 0;

            if (!task->alternative_look)
            {
               gfx_animation_ctx_entry_t entry;

               entry.easing_enum    = EASING_OUT_QUAD;
               entry.tag            = (uintptr_t)msg_widget;
               entry.duration       = MSG_QUEUE_ANIMATION_DURATION*2;
               entry.target_value   = p_dispwidget->msg_queue_height / 2.0f;
               entry.subject        = &msg_widget->msg_transition_animation;
               entry.cb             = msg_widget_msg_transition_animation_done;
               entry.userdata       = msg_widget;

               gfx_animation_push(&entry);
            }
            else
               msg_widget_msg_transition_animation_done(msg_widget);

            msg_widget->task_count++;

            msg_widget->width = new_width;
         }

         msg_widget->task_error        = !string_is_empty(task->error);
         msg_widget->task_cancelled    = task->cancelled;
         msg_widget->task_finished     = task->finished;
         msg_widget->task_progress     = task->progress;
      }
   }
}

static void gfx_widgets_unfold_end(void *userdata)
{
   menu_widget_msg_t *unfold        = (menu_widget_msg_t*)userdata;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   unfold->unfolding                = false;
   p_dispwidget->widgets_moving     = false;
}

static void gfx_widgets_move_end(void *userdata)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (userdata)
   {
      gfx_animation_ctx_entry_t entry;
      menu_widget_msg_t *unfold = (menu_widget_msg_t*)userdata;

      entry.cb             = gfx_widgets_unfold_end;
      entry.duration       = MSG_QUEUE_ANIMATION_DURATION;
      entry.easing_enum    = EASING_OUT_QUAD;
      entry.subject        = &unfold->unfold;
      entry.tag            = (uintptr_t)unfold;
      entry.target_value   = 1.0f;
      entry.userdata       = unfold;

      gfx_animation_push(&entry);

      unfold->unfolded  = true;
      unfold->unfolding = true;
   }
   else
      p_dispwidget->widgets_moving = false;
}

static void gfx_widgets_msg_queue_expired(void *userdata)
{
   menu_widget_msg_t *msg = (menu_widget_msg_t *)userdata;

   if (msg && !msg->expired)
      msg->expired = true;
}

static void gfx_widgets_msg_queue_move(dispgfx_widget_t *p_dispwidget)
{
   int i;
   float y = 0;
   /* there should always be one and only one unfolded message */
   menu_widget_msg_t *unfold        = NULL; 

   for (i = (int)(p_dispwidget->current_msgs->size-1); i >= 0; i--)
   {
      menu_widget_msg_t *msg = (menu_widget_msg_t*)
         p_dispwidget->current_msgs->list[i].userdata;

      if (!msg || msg->dying)
         continue;

      y += p_dispwidget->msg_queue_height 
         / (msg->task_ptr ? 2 : 1) + p_dispwidget->msg_queue_spacing;

      if (!msg->unfolded)
         unfold = msg;

      if (msg->offset_y != y)
      {
         gfx_animation_ctx_entry_t entry;

         entry.cb             = (i == 0) ? gfx_widgets_move_end : NULL;
         entry.duration       = MSG_QUEUE_ANIMATION_DURATION;
         entry.easing_enum    = EASING_OUT_QUAD;
         entry.subject        = &msg->offset_y;
         entry.tag            = (uintptr_t)msg;
         entry.target_value   = y;
         entry.userdata       = unfold;

         gfx_animation_push(&entry);

         p_dispwidget->widgets_moving = true;
      }
   }
}

static void gfx_widgets_msg_queue_free(
      dispgfx_widget_t *p_dispwidget,
      menu_widget_msg_t *msg, bool touch_list)
{
   size_t i;
   uintptr_t tag = (uintptr_t)msg;

   if (msg->task_ptr)
   {
      /* remove the reference the task has of ourself
         only if the task is not finished already
         (finished tasks are freed before the widget) */
      if (!msg->task_finished && !msg->task_error && !msg->task_cancelled)
         msg->task_ptr->frontend_userdata = NULL;

      /* update tasks count */
      p_dispwidget->msg_queue_tasks_count--;
   }

   /* Kill all animations */
   gfx_timer_kill(&msg->hourglass_timer);
   gfx_animation_kill_by_tag(&tag);

   /* Kill all timers */
   if (msg->expiration_timer_started)
      gfx_timer_kill(&msg->expiration_timer);

   /* Free it */
   if (msg->msg)
      free(msg->msg);

   if (msg->msg_new)
      free(msg->msg_new);

   /* Remove it from the list */
   if (touch_list)
   {
      file_list_free_userdata(p_dispwidget->current_msgs,
            p_dispwidget->msg_queue_kill);

      for (i = p_dispwidget->msg_queue_kill; i < 
            p_dispwidget->current_msgs->size-1; i++)
         p_dispwidget->current_msgs->list[i] = 
            p_dispwidget->current_msgs->list[i+1];

      p_dispwidget->current_msgs->size--;
   }

   p_dispwidget->widgets_moving = false;
}

static void gfx_widgets_msg_queue_kill_end(void *userdata)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   menu_widget_msg_t           *msg = (menu_widget_msg_t*)
      p_dispwidget->current_msgs->list[p_dispwidget->msg_queue_kill].userdata;

   if (msg)
      gfx_widgets_msg_queue_free(p_dispwidget, msg, true);
}

static void gfx_widgets_msg_queue_kill(
      dispgfx_widget_t *p_dispwidget,
      unsigned idx)
{
   gfx_animation_ctx_entry_t entry;
   menu_widget_msg_t           *msg = (menu_widget_msg_t*)
      p_dispwidget->current_msgs->list[idx].userdata;

   if (!msg)
      return;

   p_dispwidget->widgets_moving = true;
   msg->dying                   = true;

   p_dispwidget->msg_queue_kill = idx;

   /* Drop down */
   entry.cb                     = NULL;
   entry.duration               = MSG_QUEUE_ANIMATION_DURATION;
   entry.easing_enum            = EASING_OUT_QUAD;
   entry.tag                    = (uintptr_t)msg;
   entry.userdata               = NULL;
   entry.subject                = &msg->offset_y;
   entry.target_value           = msg->offset_y - 
      p_dispwidget->msg_queue_height / 4;

   gfx_animation_push(&entry);

   /* Fade out */
   entry.cb                     = gfx_widgets_msg_queue_kill_end;
   entry.subject                = &msg->alpha;
   entry.target_value           = 0.0f;

   gfx_animation_push(&entry);

   /* Move all messages back to their correct position */
   if (p_dispwidget->current_msgs->size != 0)
      gfx_widgets_msg_queue_move(p_dispwidget);
}

void gfx_widgets_draw_icon(
      void *userdata,
      unsigned video_width,
      unsigned video_height,
      unsigned icon_width,
      unsigned icon_height,
      uintptr_t texture,
      float x, float y,
      unsigned width, unsigned height,
      float rotation, float scale_factor,
      float *color)
{
   gfx_display_ctx_rotate_draw_t rotate_draw;
   gfx_display_ctx_draw_t draw;
   struct video_coords coords;
   math_matrix_4x4 mymat;

   if (!texture)
      return;

   rotate_draw.matrix       = &mymat;
   rotate_draw.rotation     = rotation;
   rotate_draw.scale_x      = scale_factor;
   rotate_draw.scale_y      = scale_factor;
   rotate_draw.scale_z      = 1;
   rotate_draw.scale_enable = true;

   gfx_display_rotate_z(&rotate_draw, userdata);

   coords.vertices      = 4;
   coords.vertex        = NULL;
   coords.tex_coord     = NULL;
   coords.lut_tex_coord = NULL;
   coords.color         = color;

   draw.x               = x;
   draw.y               = height - y - icon_height;
   draw.width           = icon_width;
   draw.height          = icon_height;
   draw.scale_factor    = scale_factor;
   draw.rotation        = rotation;
   draw.coords          = &coords;
   draw.matrix_data     = &mymat;
   draw.texture         = texture;
   draw.prim_type       = GFX_DISPLAY_PRIM_TRIANGLESTRIP;
   draw.pipeline.id     = 0;

   gfx_display_draw(&draw, userdata,
         video_width, video_height);
}

#ifdef HAVE_TRANSLATE
static void gfx_widgets_draw_icon_blend(
      void *userdata,
      unsigned video_width,
      unsigned video_height,
      unsigned icon_width,
      unsigned icon_height,
      uintptr_t texture,
      float x, float y,
      unsigned width, unsigned height,
      float rotation, float scale_factor,
      float *color)
{
   gfx_display_ctx_rotate_draw_t rotate_draw;
   gfx_display_ctx_draw_t draw;
   struct video_coords coords;
   math_matrix_4x4 mymat;

   if (!texture)
      return;

   rotate_draw.matrix       = &mymat;
   rotate_draw.rotation     = rotation;
   rotate_draw.scale_x      = scale_factor;
   rotate_draw.scale_y      = scale_factor;
   rotate_draw.scale_z      = 1;
   rotate_draw.scale_enable = true;

   gfx_display_rotate_z(&rotate_draw, userdata);

   coords.vertices      = 4;
   coords.vertex        = NULL;
   coords.tex_coord     = NULL;
   coords.lut_tex_coord = NULL;
   coords.color         = color;

   draw.x               = x;
   draw.y               = height - y - icon_height;
   draw.width           = icon_width;
   draw.height          = icon_height;
   draw.scale_factor    = scale_factor;
   draw.rotation        = rotation;
   draw.coords          = &coords;
   draw.matrix_data     = &mymat;
   draw.texture         = texture;
   draw.prim_type       = GFX_DISPLAY_PRIM_TRIANGLESTRIP;
   draw.pipeline.id     = 0;

   gfx_display_draw_blend(&draw, userdata,
         video_width, video_height);
}
#endif

void gfx_widgets_draw_text(
      gfx_widget_font_data_t* font_data,
      const char *text,
      float x, float y,
      int width, int height,
      uint32_t color,
      enum text_alignment text_align,
      bool draw_outside)
{
   if (!font_data || string_is_empty(text))
      return;

   gfx_display_draw_text(
         font_data->font,
         text,
         x, y,
         width, height,
         color,
         text_align,
         1.0f,
         false,
         0.0f,
         draw_outside);

   font_data->usage_count++;
}

void gfx_widgets_flush_text(
      unsigned video_width, unsigned video_height,
      gfx_widget_font_data_t* font_data)
{
   /* Flushing is slow - only do it if font
    * has actually been used */
   if (!font_data || (font_data->usage_count == 0))
      return;

   font_driver_flush(video_width, video_height, font_data->font);
   font_data->raster_block.carr.coords.vertices = 0;
   font_data->usage_count                       = 0;
}

float gfx_widgets_get_thumbnail_scale_factor(
      const float dst_width, const float dst_height,
      const float image_width, const float image_height)
{
   float dst_ratio      = dst_width   / dst_height;
   float image_ratio    = image_width / image_height;

   if (dst_ratio > image_ratio)
      return (dst_height / image_height);
   return (dst_width / image_width);
}

static void gfx_widgets_start_msg_expiration_timer(
      menu_widget_msg_t *msg_widget, unsigned duration)
{
   gfx_timer_ctx_entry_t timer;

   timer.cb       = gfx_widgets_msg_queue_expired;
   timer.duration = duration;
   timer.userdata = msg_widget;

   gfx_timer_start(&msg_widget->expiration_timer, &timer);

   msg_widget->expiration_timer_started = true;
}

static void gfx_widgets_hourglass_tick(void *userdata);

static void gfx_widgets_hourglass_end(void *userdata)
{
   gfx_timer_ctx_entry_t timer;
   menu_widget_msg_t *msg  = (menu_widget_msg_t*)userdata;

   msg->hourglass_rotation = 0.0f;

   timer.cb                = gfx_widgets_hourglass_tick;
   timer.duration          = HOURGLASS_INTERVAL;
   timer.userdata          = msg;

   gfx_timer_start(&msg->hourglass_timer, &timer);
}

static void gfx_widgets_hourglass_tick(void *userdata)
{
   gfx_animation_ctx_entry_t entry;
   menu_widget_msg_t *msg = (menu_widget_msg_t*)userdata;
   uintptr_t          tag = (uintptr_t)msg;

   entry.easing_enum      = EASING_OUT_QUAD;
   entry.tag              = tag;
   entry.duration         = HOURGLASS_DURATION;
   entry.target_value     = -(2 * M_PI);
   entry.subject          = &msg->hourglass_rotation;
   entry.cb               = gfx_widgets_hourglass_end;
   entry.userdata         = msg;

   gfx_animation_push(&entry);
}

void gfx_widgets_iterate(
      unsigned width, unsigned height, bool fullscreen,
      const char *dir_assets, char *font_path,
      bool is_threaded)
{
   size_t i;
   float scale_factor;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (!p_dispwidget->widgets_active)
      return;

   /* Check whether screen dimensions or menu scale
    * factor have changed */
   scale_factor = (gfx_display_get_driver_id() == MENU_DRIVER_ID_XMB) ?
         gfx_display_get_widget_pixel_scale(width, height, fullscreen) :
               gfx_display_get_widget_dpi_scale(width, height, fullscreen);

   if ((scale_factor != p_dispwidget->last_scale_factor) ||
       (width  != p_dispwidget->last_video_width) ||
       (height != p_dispwidget->last_video_height))
   {
      p_dispwidget->last_scale_factor = scale_factor;
      p_dispwidget->last_video_width  = width;
      p_dispwidget->last_video_height = height;

      /* Note: We don't need a full context reset here
       * > Just rescale layout, and reset frame time counter */
      gfx_widgets_layout(p_dispwidget,
            is_threaded, dir_assets, font_path);
      video_driver_monitor_reset();
   }

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->iterate)
         widget->iterate(p_dispwidget,
               width, height, fullscreen,
               dir_assets, font_path, is_threaded);
   }

   /* Messages queue */

   /* Consume one message if available */
   if ((fifo_read_avail(p_dispwidget->msg_queue) > 0)
         && !p_dispwidget->widgets_moving 
         && (p_dispwidget->current_msgs->size < MSG_QUEUE_ONSCREEN_MAX))
   {
      menu_widget_msg_t *msg_widget;

      fifo_read(p_dispwidget->msg_queue, &msg_widget, sizeof(msg_widget));

      /* Task messages always appear from the bottom of the screen */
      if (p_dispwidget->msg_queue_tasks_count == 0 || msg_widget->task_ptr)
      {
         file_list_append(p_dispwidget->current_msgs,
            NULL,
            NULL,
            0,
            0,
            0
         );

         file_list_set_userdata(p_dispwidget->current_msgs,
               p_dispwidget->current_msgs->size-1, msg_widget);
      }
      /* Regular messages are always above tasks */
      else
      {
        unsigned idx = (unsigned)(p_dispwidget->current_msgs->size - 
              p_dispwidget->msg_queue_tasks_count);
         file_list_insert(p_dispwidget->current_msgs,
            NULL,
            NULL,
            0,
            0,
            0,
            idx
         );

         file_list_set_userdata(p_dispwidget->current_msgs, idx, msg_widget);
      }

      /* Start expiration timer if not associated to a task */
      if (!msg_widget->task_ptr)
      {
         if (!msg_widget->expiration_timer_started)
            gfx_widgets_start_msg_expiration_timer(
                  msg_widget, MSG_QUEUE_ANIMATION_DURATION * 2 
                  + msg_widget->duration);
      }
      /* Else, start hourglass animation timer */
      else
      {
         p_dispwidget->msg_queue_tasks_count++;
         gfx_widgets_hourglass_end(msg_widget);
      }

      if (p_dispwidget->current_msgs->size != 0)
         gfx_widgets_msg_queue_move(p_dispwidget);
   }

   /* Kill first expired message */
   /* Start expiration timer of dead tasks */
   for (i = 0; i < p_dispwidget->current_msgs->size ; i++)
   {
      menu_widget_msg_t *msg_widget = (menu_widget_msg_t*)
         p_dispwidget->current_msgs->list[i].userdata;

      if (!msg_widget)
         continue;

      if (msg_widget->task_ptr && (msg_widget->task_finished 
               || msg_widget->task_cancelled))
         if (!msg_widget->expiration_timer_started)
            gfx_widgets_start_msg_expiration_timer(msg_widget, TASK_FINISHED_DURATION);

      if (msg_widget->expired && !p_dispwidget->widgets_moving)
      {
         gfx_widgets_msg_queue_kill(p_dispwidget,
               (unsigned)i);
         break;
      }
   }
}

static int gfx_widgets_draw_indicator(
      dispgfx_widget_t *p_dispwidget,
      void *userdata, 
      unsigned video_width,
      unsigned video_height,
      uintptr_t icon, int y, int top_right_x_advance, 
      enum msg_hash_enums msg)
{
   unsigned width;

   gfx_display_set_alpha(gfx_widgets_backdrop_orig, DEFAULT_BACKDROP);

   if (icon)
   {
      unsigned height = p_dispwidget->simple_widget_height * 2;
      width  = height;

      gfx_display_draw_quad(userdata,
         video_width, video_height,
         top_right_x_advance - width, y,
         width, height,
         video_width, video_height,
         gfx_widgets_backdrop_orig
      );

      gfx_display_set_alpha(gfx_widgets_pure_white, 1.0f);

      gfx_display_blend_begin(userdata);
      gfx_widgets_draw_icon(
            userdata,
            video_width,
            video_height,
            width, height,
            icon, top_right_x_advance - width, y,
            video_width, video_height,
            0, 1, gfx_widgets_pure_white
            );
      gfx_display_blend_end(userdata);
   }
   else
   {
      unsigned height       = p_dispwidget->simple_widget_height;
      const char *txt       = msg_hash_to_str(msg);

      width = font_driver_get_message_width(p_dispwidget->gfx_widget_fonts.regular.font,
            txt,
            (unsigned)strlen(txt), 1) + p_dispwidget->simple_widget_padding * 2;

      gfx_display_draw_quad(userdata,
            video_width, video_height,
            top_right_x_advance - width, y,
            width, height,
            video_width, video_height,
            gfx_widgets_backdrop_orig
      );

      gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.regular,
            txt,
            top_right_x_advance - width 
            + p_dispwidget->simple_widget_padding,
            y + (height / 2.0f) + 
            p_dispwidget->gfx_widget_fonts.regular.line_centre_offset,
            video_width, video_height,
            0xFFFFFFFF, TEXT_ALIGN_LEFT,
            false);
   }

   return width;
}

static void gfx_widgets_draw_task_msg(
      dispgfx_widget_t *p_dispwidget,
      menu_widget_msg_t *msg,
      void *userdata,
      unsigned video_width,
      unsigned video_height)
{
   unsigned text_color;
   unsigned bar_width;

   unsigned rect_x;
   unsigned rect_y;
   unsigned rect_width;
   unsigned rect_height;
   float text_y_base;

   float *msg_queue_current_background;
   float *msg_queue_current_bar;

   bool draw_msg_new                = false;
   unsigned task_percentage_offset  = 0;
   char task_percentage[256]        = {0};

   if (msg->msg_new)
      draw_msg_new = !string_is_equal(msg->msg_new, msg->msg);

   task_percentage_offset = p_dispwidget->gfx_widget_fonts.msg_queue.glyph_width * (msg->task_error ? 12 : 5) 
      + p_dispwidget->simple_widget_padding * 1.25f; /*11 = strlen("Task failed")+1 */

   if (msg->task_finished)
   {
      if (msg->task_error)
         strlcpy(task_percentage, "Task failed", sizeof(task_percentage));
      else
         strlcpy(task_percentage, " ", sizeof(task_percentage));
   }
   else if (msg->task_progress >= 0 && msg->task_progress <= 100)
      snprintf(task_percentage, sizeof(task_percentage),
            "%i%%", msg->task_progress);

   rect_width = p_dispwidget->simple_widget_padding 
      + msg->width 
      + task_percentage_offset;
   bar_width  = rect_width * msg->task_progress/100.0f;
   text_color = COLOR_TEXT_ALPHA(0xFFFFFF00, (unsigned)(msg->alpha*255.0f));

   /* Rect */
   if (msg->task_finished)
      if (msg->task_count == 1)
         msg_queue_current_background = msg_queue_task_progress_1;
      else
         msg_queue_current_background = msg_queue_task_progress_2;
   else
      if (msg->task_count == 1)
         msg_queue_current_background = msg_queue_background;
      else
         msg_queue_current_background = msg_queue_task_progress_1;

   rect_x      = p_dispwidget->msg_queue_rect_start_x - p_dispwidget->msg_queue_icon_size_x;
   rect_y      = video_height - msg->offset_y;
   rect_height = p_dispwidget->msg_queue_height / 2;

   gfx_display_set_alpha(msg_queue_current_background, msg->alpha);
   gfx_display_draw_quad(userdata,
         video_width, video_height,
         rect_x, rect_y,
         rect_width, rect_height,
         video_width, video_height,
         msg_queue_current_background
         );

   /* Progress bar */
   if (!msg->task_finished && msg->task_progress >= 0 && msg->task_progress <= 100)
   {
      if (msg->task_count == 1)
         msg_queue_current_bar = msg_queue_task_progress_1;
      else
         msg_queue_current_bar = msg_queue_task_progress_2;

      gfx_display_set_alpha(msg_queue_current_bar, 1.0f);
      gfx_display_draw_quad(userdata,
            video_width, video_height,
            p_dispwidget->msg_queue_task_rect_start_x, video_height - msg->offset_y,
            bar_width, rect_height,
            video_width, video_height,
            msg_queue_current_bar
            );
   }

   /* Icon */
   gfx_display_set_alpha(gfx_widgets_pure_white, msg->alpha);
   gfx_display_blend_begin(userdata);
   gfx_widgets_draw_icon(
         userdata,
         video_width,
         video_height,
         p_dispwidget->msg_queue_height / 2,
         p_dispwidget->msg_queue_height / 2,
         p_dispwidget->gfx_widgets_icons_textures[
         msg->task_finished 
         ? MENU_WIDGETS_ICON_CHECK 
         : MENU_WIDGETS_ICON_HOURGLASS],
         p_dispwidget->msg_queue_task_hourglass_x,
         video_height - msg->offset_y,
         video_width,
         video_height,
         msg->task_finished ? 0 : msg->hourglass_rotation,
         1, gfx_widgets_pure_white);
   gfx_display_blend_end(userdata);

   /* Text */
   text_y_base = video_height 
      - msg->offset_y 
      + p_dispwidget->msg_queue_height / 4.0f 
      + p_dispwidget->gfx_widget_fonts.msg_queue.line_centre_offset;

   if (draw_msg_new)
   {
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.msg_queue);

      gfx_display_scissor_begin(userdata,
            video_width, video_height,
            rect_x, rect_y, rect_width, rect_height);

      gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.msg_queue,
            msg->msg_new,
            p_dispwidget->msg_queue_task_text_start_x,
            text_y_base 
            - p_dispwidget->msg_queue_height / 2.0f 
            + msg->msg_transition_animation,
            video_width, video_height,
            text_color,
            TEXT_ALIGN_LEFT,
            true);
   }

   gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.msg_queue,
         msg->msg,
         p_dispwidget->msg_queue_task_text_start_x,
         text_y_base + msg->msg_transition_animation,
         video_width, video_height,
         text_color,
         TEXT_ALIGN_LEFT,
         true);

   if (draw_msg_new)
   {
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.msg_queue);
      gfx_display_scissor_end(userdata,
            video_width, video_height);
   }

   /* Progress text */
   text_color = COLOR_TEXT_ALPHA(0xFFFFFF00, (unsigned)(msg->alpha/2*255.0f));
   gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.msg_queue,
      task_percentage,
      p_dispwidget->msg_queue_rect_start_x - p_dispwidget->msg_queue_icon_size_x + rect_width - 
      p_dispwidget->gfx_widget_fonts.msg_queue.glyph_width,
      text_y_base,
      video_width, video_height,
      text_color,
      TEXT_ALIGN_RIGHT,
      true);
}

static void gfx_widgets_draw_regular_msg(
      dispgfx_widget_t *p_dispwidget,
      menu_widget_msg_t *msg,
      void *userdata,
      unsigned video_width,
      unsigned video_height)
{
   unsigned bar_width;
   unsigned text_color;
   uintptr_t            icon        = 0;

   if (!icon)
      icon = p_dispwidget->gfx_widgets_icons_textures[
         MENU_WIDGETS_ICON_INFO]; /* TODO: Real icon logic here */

   /* Icon */
   gfx_display_set_alpha(msg_queue_info, msg->alpha);
   gfx_display_set_alpha(gfx_widgets_pure_white, msg->alpha);
   gfx_display_set_alpha(msg_queue_background, msg->alpha);

   if (!msg->unfolded || msg->unfolding)
   {
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.regular);
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.bold);
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.msg_queue);

      gfx_display_scissor_begin(userdata,
            video_width, video_height,
            p_dispwidget->msg_queue_scissor_start_x, 0,
            (p_dispwidget->msg_queue_scissor_start_x + msg->width - 
             p_dispwidget->simple_widget_padding * 2) 
            * msg->unfold, video_height);
   }

   if (p_dispwidget->msg_queue_has_icons)
   {
      gfx_display_blend_begin(userdata);
      /* (int) cast is to be consistent with the rect drawing 
       * and prevent alignment issues, don't remove it */
      gfx_widgets_draw_icon(
            userdata,
            video_width,
            video_height,
            p_dispwidget->msg_queue_icon_size_x,
            p_dispwidget->msg_queue_icon_size_y,
            p_dispwidget->msg_queue_icon_rect,
            p_dispwidget->msg_queue_spacing,
            (int)(video_height - msg->offset_y - p_dispwidget->msg_queue_icon_offset_y),
            video_width, video_height, 
            0, 1, msg_queue_background);

      gfx_display_blend_end(userdata);
   }

   /* Background */
   bar_width = p_dispwidget->simple_widget_padding + msg->width;

   gfx_display_draw_quad(
         userdata,
         video_width,
         video_height,
         p_dispwidget->msg_queue_rect_start_x,
         video_height - msg->offset_y,
         bar_width,
         p_dispwidget->msg_queue_height,
         video_width,
         video_height,
         msg_queue_background
         );

   /* Text */
   text_color = COLOR_TEXT_ALPHA(0xFFFFFF00, (unsigned)(msg->alpha*255.0f));

   gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.msg_queue,
      msg->msg,
      p_dispwidget->msg_queue_regular_text_start - ((1.0f-msg->unfold) * msg->width/2),
      video_height - msg->offset_y + (p_dispwidget->msg_queue_height - msg->text_height)/2.0f + p_dispwidget->gfx_widget_fonts.msg_queue.line_ascender,
      video_width, video_height,
      text_color,
      TEXT_ALIGN_LEFT,
      true);

   if (!msg->unfolded || msg->unfolding)
   {
      gfx_widgets_flush_text(video_width, video_height, &p_dispwidget->gfx_widget_fonts.regular);
      gfx_widgets_flush_text(video_width, video_height, &p_dispwidget->gfx_widget_fonts.bold);
      gfx_widgets_flush_text(video_width, video_height, &p_dispwidget->gfx_widget_fonts.msg_queue);

      gfx_display_scissor_end(userdata,
            video_width, video_height);
   }

   if (p_dispwidget->msg_queue_has_icons)
   {
      gfx_display_blend_begin(userdata);

      gfx_widgets_draw_icon(
            userdata,
            video_width,
            video_height,
            p_dispwidget->msg_queue_icon_size_x,
            p_dispwidget->msg_queue_icon_size_y,
            p_dispwidget->msg_queue_icon,
            p_dispwidget->msg_queue_spacing, video_height 
            - msg->offset_y - p_dispwidget->msg_queue_icon_offset_y, 
            video_width, video_height,
            0, 1, msg_queue_info);

      gfx_widgets_draw_icon(
            userdata,
            video_width,
            video_height,
            p_dispwidget->msg_queue_icon_size_x,
            p_dispwidget->msg_queue_icon_size_y,
            p_dispwidget->msg_queue_icon_outline,
            p_dispwidget->msg_queue_spacing,
            video_height - msg->offset_y - p_dispwidget->msg_queue_icon_offset_y, 
            video_width, video_height,
            0, 1, gfx_widgets_pure_white);

      gfx_widgets_draw_icon(
            userdata,
            video_width,
            video_height,
            p_dispwidget->msg_queue_internal_icon_size, p_dispwidget->msg_queue_internal_icon_size,
            icon, p_dispwidget->msg_queue_spacing + p_dispwidget->msg_queue_internal_icon_offset,
            video_height - msg->offset_y - p_dispwidget->msg_queue_icon_offset_y + p_dispwidget->msg_queue_internal_icon_offset, 
            video_width, video_height,
            0, 1, gfx_widgets_pure_white);

      gfx_display_blend_end(userdata);
   }
}

#ifdef HAVE_MENU
static void gfx_widgets_draw_backdrop(
      void *userdata,
      unsigned video_width,
      unsigned video_height,
      float alpha)
{
   gfx_display_set_alpha(gfx_widgets_backdrop, alpha);
   gfx_display_draw_quad(userdata,
         video_width, video_height, 0, 0,
         video_width, video_height, video_width, video_height,
         gfx_widgets_backdrop);
}

static void gfx_widgets_draw_load_content_animation(
      dispgfx_widget_t *p_dispwidget,
      void *userdata,
      unsigned video_width,
      unsigned video_height)
{
   /* TODO: change metrics? */
   int icon_size         = (int)p_dispwidget->load_content_animation_icon_size;
   uint32_t text_alpha   = p_dispwidget->load_content_animation_fade_alpha * 255.0f;
   uint32_t text_color   = COLOR_TEXT_ALPHA(0xB8B8B800, text_alpha);
   unsigned text_offset  = -25 * p_dispwidget->last_scale_factor * 
      p_dispwidget->load_content_animation_fade_alpha;
   float *icon_color     = p_dispwidget->load_content_animation_icon_color;

   /* Fade out */
   gfx_widgets_draw_backdrop(userdata,
         video_width, video_height,
         p_dispwidget->load_content_animation_fade_alpha);

   /* Icon */
   gfx_display_set_alpha(icon_color,
         p_dispwidget->load_content_animation_icon_alpha);
   gfx_display_blend_begin(userdata);
   gfx_widgets_draw_icon(
         userdata,
         video_width,
         video_height,
         icon_size,
         icon_size,
         p_dispwidget->load_content_animation_icon,
         video_width  / 2 - icon_size/2,
         video_height / 2 - icon_size/2,
         video_width,
         video_height,
         0, 1, icon_color
         );
   gfx_display_blend_end(userdata);

   /* Text
    * TODO/FIXME: Check vertical alignment - where is
    * this text actually meant to go...? */
   gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.bold,
         p_dispwidget->load_content_animation_content_name,
         video_width  / 2,
         video_height / 2 + (175 + 25) * p_dispwidget->last_scale_factor 
         + text_offset 
         + p_dispwidget->gfx_widget_fonts.bold.line_centre_offset,
         video_width,
         video_height,
         text_color,
         TEXT_ALIGN_CENTER,
         false);

   /* Flush text layer */
   gfx_widgets_flush_text(video_width, video_height,
         &p_dispwidget->gfx_widget_fonts.regular);
   gfx_widgets_flush_text(video_width, video_height,
         &p_dispwidget->gfx_widget_fonts.bold);
   gfx_widgets_flush_text(video_width, video_height,
         &p_dispwidget->gfx_widget_fonts.msg_queue);

   /* Everything disappears */
   gfx_widgets_draw_backdrop(userdata,
         video_width, video_height,
         p_dispwidget->load_content_animation_final_fade_alpha);
}
#endif

static void INLINE gfx_widgets_font_bind(gfx_widget_font_data_t *font_data)
{
   font_driver_bind_block(font_data->font, &font_data->raster_block);
   font_data->raster_block.carr.coords.vertices = 0;
   font_data->usage_count                       = 0;
}

static void INLINE gfx_widgets_font_unbind(gfx_widget_font_data_t *font_data)
{
   font_driver_bind_block(font_data->font, NULL);
}

void gfx_widgets_frame(void *data)
{
   /* (Pointless) optimisation: allocating these
    * costs nothing, so do it *before* the
    * 'widgets_active' check... */
   size_t i;
   video_frame_info_t *video_info;
   bool framecount_show;
   bool memory_show;
   bool core_status_msg_show;
   void *userdata;
   unsigned video_width;
   unsigned video_height;
   bool widgets_is_paused;
   bool fps_show;
   bool widgets_is_fastforwarding;
   bool widgets_is_rewinding;
   bool runloop_is_slowmotion;
   int top_right_x_advance;
#ifdef HAVE_CHEEVOS
   int scissor_me_timbers           = 0;
#endif
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (!p_dispwidget->widgets_active)
      return;

   /* ...but assigning them costs a tiny amount,
    * so do it *after* the 'widgets_active' check */
   video_info                = (video_frame_info_t*)data;
   framecount_show           = video_info->framecount_show;
   memory_show               = video_info->memory_show;
   core_status_msg_show      = video_info->core_status_msg_show;
   userdata                  = video_info->userdata;
   video_width               = video_info->width;
   video_height              = video_info->height;
   widgets_is_paused         = video_info->widgets_is_paused;
   fps_show                  = video_info->fps_show;
   widgets_is_fastforwarding = video_info->widgets_is_fast_forwarding;
   widgets_is_rewinding      = video_info->widgets_is_rewinding;
   runloop_is_slowmotion     = video_info->runloop_is_slowmotion;
   top_right_x_advance       = video_width;

   p_dispwidget->gfx_widgets_frame_count++;

   gfx_display_set_viewport(video_width, video_height);

   /* Font setup */
   gfx_widgets_font_bind(&p_dispwidget->gfx_widget_fonts.regular);
   gfx_widgets_font_bind(&p_dispwidget->gfx_widget_fonts.bold);
   gfx_widgets_font_bind(&p_dispwidget->gfx_widget_fonts.msg_queue);

#ifdef HAVE_TRANSLATE
   /* AI Service overlay */
   if (p_dispwidget->ai_service_overlay_state > 0)
   {
      float outline_color[16] = {
      0.00, 1.00, 0.00, 1.00,
      0.00, 1.00, 0.00, 1.00,
      0.00, 1.00, 0.00, 1.00,
      0.00, 1.00, 0.00, 1.00,
      };
      gfx_display_set_alpha(gfx_widgets_pure_white, 1.0f);

      gfx_widgets_draw_icon_blend(
            userdata,
            video_width,
            video_height,
            video_width,
            video_height,
            p_dispwidget->ai_service_overlay_texture,
            0,
            0,
            video_width,
            video_height,
            0,
            1,
            gfx_widgets_pure_white
            );
      /* top line */
      gfx_display_draw_quad(userdata,
            video_width, video_height,
            0, 0,
            video_width,
            p_dispwidget->divider_width_1px,
            video_width,
            video_height,
            outline_color
            );
      /* bottom line */
      gfx_display_draw_quad(userdata,
            video_width, video_height,
            0,
            video_height - p_dispwidget->divider_width_1px,
            video_width,
            p_dispwidget->divider_width_1px,
            video_width,
            video_height,
            outline_color
            );
      /* left line */
      gfx_display_draw_quad(userdata,
            video_width,
            video_height,
            0,
            0,
            p_dispwidget->divider_width_1px,
            video_height,
            video_width,
            video_height,
            outline_color
            );
      /* right line */
      gfx_display_draw_quad(userdata,
            video_width, video_height,
            video_width - p_dispwidget->divider_width_1px,
            0,
            p_dispwidget->divider_width_1px,
            video_height,
            video_width,
            video_height,
            outline_color
            );

      if (p_dispwidget->ai_service_overlay_state == 2)
          p_dispwidget->ai_service_overlay_state = 3;
   }
#endif

#ifdef HAVE_CHEEVOS
   /* Achievement notification */
   if (p_dispwidget->cheevo_popup_queue_read_index >= 0 && 
         p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].title)
   {
      SLOCK_LOCK(p_dispwidget->cheevo_popup_queue_lock);

      if (p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_read_index].title)
      {
         unsigned unfold_offet = ((1.0f - p_dispwidget->cheevo_unfold) 
               * p_dispwidget->cheevo_width / 2);

         gfx_display_set_alpha(gfx_widgets_backdrop_orig, DEFAULT_BACKDROP);
         gfx_display_set_alpha(gfx_widgets_pure_white, 1.0f);

         /* Default icon */
         if (!p_dispwidget->cheevo_popup_queue[
               p_dispwidget->cheevo_popup_queue_read_index].badge)
         {
            /* Backdrop */
            gfx_display_draw_quad(userdata,
                  video_width, video_height,
                  0, (int)p_dispwidget->cheevo_y,
                  p_dispwidget->cheevo_height,
                  p_dispwidget->cheevo_height,
                  video_width, video_height,
                  gfx_widgets_backdrop_orig);

            /* Icon */
            if (p_dispwidget->gfx_widgets_icons_textures[
                  MENU_WIDGETS_ICON_ACHIEVEMENT])
            {
               gfx_display_blend_begin(userdata);
               gfx_widgets_draw_icon(
                     userdata,
                     video_width,
                     video_height,
                     p_dispwidget->cheevo_height,
                     p_dispwidget->cheevo_height,
                     p_dispwidget->gfx_widgets_icons_textures[
                     MENU_WIDGETS_ICON_ACHIEVEMENT],
                     0,
                     p_dispwidget->cheevo_y,
                     video_width, video_height, 0, 1, gfx_widgets_pure_white);
               gfx_display_blend_end(userdata);
            }
         }
         /* Badge */
         else
         {
            gfx_widgets_draw_icon(
                  userdata,
                  video_width,
                  video_height,
                  p_dispwidget->cheevo_height,
                  p_dispwidget->cheevo_height,
                  p_dispwidget->cheevo_popup_queue[
                  p_dispwidget->cheevo_popup_queue_read_index].badge,
                  0,
                  p_dispwidget->cheevo_y,
                  video_width,
                  video_height,
                  0,
                  1,
                  gfx_widgets_pure_white);
         }

         /* I _think_ cheevo_unfold changes in another thread */
         scissor_me_timbers = (fabs(p_dispwidget->cheevo_unfold - 1.0f) > 0.01);
         if (scissor_me_timbers)
            gfx_display_scissor_begin(userdata,
                  video_width,
                  video_height,
                  p_dispwidget->cheevo_height,
                  0,
                  (unsigned)((float)(p_dispwidget->cheevo_width)
                     * p_dispwidget->cheevo_unfold),
                  p_dispwidget->cheevo_height);

         /* Backdrop */
         gfx_display_draw_quad(
               userdata,
               video_width,
               video_height,
               p_dispwidget->cheevo_height,
               (int)p_dispwidget->cheevo_y,
               p_dispwidget->cheevo_width,
               p_dispwidget->cheevo_height,
               video_width,
               video_height,
               gfx_widgets_backdrop_orig);

         /* Title */
         gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.regular,
               msg_hash_to_str(MSG_ACHIEVEMENT_UNLOCKED),
               p_dispwidget->cheevo_height 
               + p_dispwidget->simple_widget_padding - unfold_offet,
               p_dispwidget->cheevo_y 
               + p_dispwidget->gfx_widget_fonts.regular.line_height 
               + p_dispwidget->gfx_widget_fonts.regular.line_ascender,
               video_width, video_height,
               TEXT_COLOR_FAINT,
               TEXT_ALIGN_LEFT,
               true);

         /* Cheevo name */

         /* TODO: is a ticker necessary ? */
         gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.regular,
               p_dispwidget->cheevo_popup_queue[
               p_dispwidget->cheevo_popup_queue_read_index].title,
               p_dispwidget->cheevo_height 
               + p_dispwidget->simple_widget_padding - unfold_offet,
               p_dispwidget->cheevo_y 
               + p_dispwidget->cheevo_height 
               - p_dispwidget->gfx_widget_fonts.regular.line_height 
               - p_dispwidget->gfx_widget_fonts.regular.line_descender,
               video_width, video_height,
               TEXT_COLOR_INFO,
               TEXT_ALIGN_LEFT,
               true);

         if (scissor_me_timbers)
         {
            gfx_widgets_flush_text(video_width, video_height,
                  &p_dispwidget->gfx_widget_fonts.regular);
            gfx_display_scissor_end(userdata,
                  video_width, video_height);
         }
      }

      SLOCK_UNLOCK(p_dispwidget->cheevo_popup_queue_lock);
   }
#endif

   /* FPS Counter */
   if (     fps_show 
         || framecount_show
         || memory_show
         || core_status_msg_show
         )
   {
      const char *text      = *p_dispwidget->gfx_widgets_fps_text == '\0' 
         ? "N/A" : p_dispwidget->gfx_widgets_fps_text;

      int text_width        = font_driver_get_message_width(
            p_dispwidget->gfx_widget_fonts.regular.font,
            text,
            (unsigned)strlen(text), 1.0f);
      int total_width       = text_width 
         + p_dispwidget->simple_widget_padding * 2;

      int fps_text_x        = top_right_x_advance 
         - p_dispwidget->simple_widget_padding - text_width;
      /* Ensure that left hand side of text does
       * not bleed off the edge of the screen */
      fps_text_x            = (fps_text_x < 0) ? 0 : fps_text_x;

      gfx_display_set_alpha(gfx_widgets_backdrop_orig, DEFAULT_BACKDROP);

      gfx_display_draw_quad(userdata,
            video_width,
            video_height,
            top_right_x_advance - total_width, 0,
            total_width,
            p_dispwidget->simple_widget_height,
            video_width,
            video_height,
            gfx_widgets_backdrop_orig
            );

      gfx_widgets_draw_text(&p_dispwidget->gfx_widget_fonts.regular,
            text,
            fps_text_x,
            p_dispwidget->simple_widget_height / 2.0f 
            + p_dispwidget->gfx_widget_fonts.regular.line_centre_offset,
            video_width, video_height,
            0xFFFFFFFF,
            TEXT_ALIGN_LEFT,
            true);
   }

   /* Indicators */
   if (widgets_is_paused)
      top_right_x_advance -= gfx_widgets_draw_indicator(
            p_dispwidget,
            userdata,
            video_width,
            video_height,
            p_dispwidget->gfx_widgets_icons_textures[
            MENU_WIDGETS_ICON_PAUSED],
            (fps_show 
             ? p_dispwidget->simple_widget_height 
             : 0),
            top_right_x_advance,
            MSG_PAUSED);

   if (widgets_is_fastforwarding)
      top_right_x_advance -= gfx_widgets_draw_indicator(
            p_dispwidget,
            userdata,
            video_width,
            video_height,
            p_dispwidget->gfx_widgets_icons_textures[
            MENU_WIDGETS_ICON_FAST_FORWARD],
            (fps_show ? p_dispwidget->simple_widget_height : 0),
            top_right_x_advance,
            MSG_FAST_FORWARD);

   if (widgets_is_rewinding)
      top_right_x_advance -= gfx_widgets_draw_indicator(
            p_dispwidget,
            userdata,
            video_width,
            video_height,
            p_dispwidget->gfx_widgets_icons_textures[
            MENU_WIDGETS_ICON_REWIND],
            (fps_show ? p_dispwidget->simple_widget_height : 0),
            top_right_x_advance,
            MSG_REWINDING);

   if (runloop_is_slowmotion)
      top_right_x_advance -= gfx_widgets_draw_indicator(
            p_dispwidget,
            userdata,
            video_width,
            video_height,
            p_dispwidget->gfx_widgets_icons_textures[
            MENU_WIDGETS_ICON_SLOW_MOTION],
            (fps_show ? p_dispwidget->simple_widget_height : 0),
            top_right_x_advance,
            MSG_SLOW_MOTION);

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->frame)
         widget->frame(data, p_dispwidget);
   }

   /* Draw all messages */
   for (i = 0; i < p_dispwidget->current_msgs->size; i++)
   {
      menu_widget_msg_t *msg = (menu_widget_msg_t*)
         p_dispwidget->current_msgs->list[i].userdata;

      if (!msg)
         continue;

      if (msg->task_ptr)
         gfx_widgets_draw_task_msg(
               p_dispwidget,
               msg, userdata,
               video_width, video_height);
      else
         gfx_widgets_draw_regular_msg(
               p_dispwidget,
               msg, userdata,
               video_width, video_height);
   }

#ifdef HAVE_MENU
   /* Load content animation */
   if (p_dispwidget->load_content_animation_running)
      gfx_widgets_draw_load_content_animation(
            p_dispwidget,
            userdata,
            video_width,
            video_height);
   else
#endif
   {
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.regular);
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.bold);
      gfx_widgets_flush_text(video_width, video_height,
            &p_dispwidget->gfx_widget_fonts.msg_queue);
   }

   /* Unbind fonts */
   gfx_widgets_font_unbind(&p_dispwidget->gfx_widget_fonts.regular);
   gfx_widgets_font_unbind(&p_dispwidget->gfx_widget_fonts.bold);
   gfx_widgets_font_unbind(&p_dispwidget->gfx_widget_fonts.msg_queue);

   gfx_display_unset_viewport(video_width, video_height);
}

bool gfx_widgets_init(
      bool video_is_threaded,
      unsigned width, unsigned height, bool fullscreen,
      const char *dir_assets, char *font_path)
{
   dispgfx_widget_t *p_dispwidget              = (dispgfx_widget_t*)
      dispwidget_get_ptr();
#ifdef HAVE_CHEEVOS
   p_dispwidget->cheevo_popup_queue_read_index = -1;
#endif
   p_dispwidget->divider_width_1px             = 1;
   p_dispwidget->gfx_widgets_generic_tag       = (uintptr_t)
      &p_dispwidget->widgets_active;
   if (!gfx_display_init_first_driver(video_is_threaded))
      goto error;

   if (!p_dispwidget->widgets_inited)
   {
      size_t i;

      p_dispwidget->gfx_widgets_frame_count = 0;

      for (i = 0; i < ARRAY_SIZE(widgets); i++)
      {
         const gfx_widget_t* widget = widgets[i];

         if (widget->init)
            widget->init(video_is_threaded, fullscreen);
      }

      p_dispwidget->msg_queue = 
         fifo_new(MSG_QUEUE_PENDING_MAX * sizeof(menu_widget_msg_t*));

      if (!p_dispwidget->msg_queue)
         goto error;

      p_dispwidget->current_msgs = (file_list_t*)
         calloc(1, sizeof(file_list_t));

      if (!p_dispwidget->current_msgs)
         goto error;

      if (!file_list_reserve(p_dispwidget->current_msgs,
               MSG_QUEUE_ONSCREEN_MAX))
         goto error;

      p_dispwidget->widgets_inited = true;
   }

   gfx_widgets_context_reset(
         p_dispwidget,
         video_is_threaded,
         width, height, fullscreen,
         dir_assets, font_path);

   p_dispwidget->widgets_active = true;

   return true;

error:
   gfx_widgets_free(p_dispwidget);
   return false;
}

bool gfx_widgets_deinit(void)
{
   dispgfx_widget_t *p_dispwidget = (dispgfx_widget_t*)dispwidget_get_ptr();
   if (!p_dispwidget->widgets_inited)
      return false;

   p_dispwidget->widgets_active     = false;
   gfx_widgets_context_destroy(p_dispwidget);

   if (!p_dispwidget->widgets_persisting)
      gfx_widgets_free(p_dispwidget);

   return true;
}

static void gfx_widgets_font_init(
      dispgfx_widget_t *p_dispwidget,
      gfx_widget_font_data_t *font_data,
      bool is_threaded, char *font_path, float font_size)
{
   int                glyph_width   = 0;
   float                scaled_size = font_size * 
      p_dispwidget->last_scale_factor;

   /* Free existing font */
   if (font_data->font)
   {
      gfx_display_font_free(font_data->font);
      font_data->font = NULL;
   }

   /* Get approximate glyph width */
   font_data->glyph_width = scaled_size * (3.0f / 4.0f);

   /* Create font */
   font_data->font = gfx_display_font_file(font_path, scaled_size, is_threaded);

   /* Get font metadata */
   glyph_width = font_driver_get_message_width(font_data->font, "a", 1, 1.0f);
   if (glyph_width > 0)
      font_data->glyph_width     = (float)glyph_width;
   font_data->line_height        = (float)font_driver_get_line_height(font_data->font, 1.0f);
   font_data->line_ascender      = (float)font_driver_get_line_ascender(font_data->font, 1.0f);
   font_data->line_descender     = (float)font_driver_get_line_descender(font_data->font, 1.0f);
   font_data->line_centre_offset = (float)font_driver_get_line_centre_offset(font_data->font, 1.0f);

   font_data->usage_count = 0;
}

static void gfx_widgets_layout(
      dispgfx_widget_t *p_dispwidget,
      bool is_threaded, const char *dir_assets, char *font_path)
{
   size_t i;

   /* Initialise fonts */
   if (string_is_empty(font_path))
   {
      char ozone_path[PATH_MAX_LENGTH];
      char font_file[PATH_MAX_LENGTH];

      ozone_path[0] = '\0';
      font_file[0]  = '\0';

      /* Base path */
      fill_pathname_join(ozone_path, dir_assets, "ozone", sizeof(ozone_path));

      /* Create regular font */
      fill_pathname_join(font_file, ozone_path, "regular.ttf", sizeof(font_file));
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.regular,
            is_threaded, font_file, BASE_FONT_SIZE);

      /* Create bold font */
      fill_pathname_join(font_file, ozone_path, "bold.ttf", sizeof(font_file));
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.bold,
            is_threaded, font_file, BASE_FONT_SIZE);

      /* Create msg_queue font */
      fill_pathname_join(font_file, ozone_path, "regular.ttf", sizeof(font_file));
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.msg_queue,
            is_threaded, font_file, MSG_QUEUE_FONT_SIZE);
   }
   else
   {
      /* Load fonts from user-supplied path */
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.regular,
            is_threaded, font_path, BASE_FONT_SIZE);
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.bold,
            is_threaded, font_path, BASE_FONT_SIZE);
      gfx_widgets_font_init(p_dispwidget,
            &p_dispwidget->gfx_widget_fonts.msg_queue,
            is_threaded, font_path, MSG_QUEUE_FONT_SIZE);
   }

   /* Calculate dimensions */
   p_dispwidget->simple_widget_padding            = p_dispwidget->gfx_widget_fonts.regular.line_height * 2.0f/3.0f;
   p_dispwidget->simple_widget_height             = p_dispwidget->gfx_widget_fonts.regular.line_height + p_dispwidget->simple_widget_padding;

   p_dispwidget->msg_queue_height                 = p_dispwidget->gfx_widget_fonts.msg_queue.line_height * 2.5f * (BASE_FONT_SIZE / MSG_QUEUE_FONT_SIZE);

   if (p_dispwidget->msg_queue_has_icons)
   {
      p_dispwidget->msg_queue_icon_size_y         = p_dispwidget->msg_queue_height 
         * 1.2347826087f; /* original image is 280x284 */
      p_dispwidget->msg_queue_icon_size_x         = 0.98591549295f * p_dispwidget->msg_queue_icon_size_y;
   }
   else
   {
      p_dispwidget->msg_queue_icon_size_x         = 0;
      p_dispwidget->msg_queue_icon_size_y         = 0;
   }

   p_dispwidget->msg_queue_spacing                = p_dispwidget->msg_queue_height / 3;
   p_dispwidget->msg_queue_rect_start_x           = p_dispwidget->msg_queue_spacing + p_dispwidget->msg_queue_icon_size_x;
   p_dispwidget->msg_queue_internal_icon_size     = p_dispwidget->msg_queue_icon_size_y;
   p_dispwidget->msg_queue_internal_icon_offset   = (p_dispwidget->msg_queue_icon_size_y - p_dispwidget->msg_queue_internal_icon_size) / 2;
   p_dispwidget->msg_queue_icon_offset_y          = (p_dispwidget->msg_queue_icon_size_y - p_dispwidget->msg_queue_height) / 2;
   p_dispwidget->msg_queue_scissor_start_x        = p_dispwidget->msg_queue_spacing + p_dispwidget->msg_queue_icon_size_x - (p_dispwidget->msg_queue_icon_size_x * 0.28928571428f);

   if (p_dispwidget->msg_queue_has_icons)
      p_dispwidget->msg_queue_regular_padding_x   = p_dispwidget->simple_widget_padding / 2;
   else
      p_dispwidget->msg_queue_regular_padding_x   = p_dispwidget->simple_widget_padding;

   p_dispwidget->msg_queue_task_rect_start_x      = p_dispwidget->msg_queue_rect_start_x - p_dispwidget->msg_queue_icon_size_x;

   p_dispwidget->msg_queue_task_text_start_x      = p_dispwidget->msg_queue_task_rect_start_x + p_dispwidget->msg_queue_height / 2;

   if (!p_dispwidget->gfx_widgets_icons_textures[MENU_WIDGETS_ICON_HOURGLASS])
      p_dispwidget->msg_queue_task_text_start_x         -= 
         p_dispwidget->gfx_widget_fonts.msg_queue.glyph_width * 2.0f;

   p_dispwidget->msg_queue_regular_text_start            = p_dispwidget->msg_queue_rect_start_x + p_dispwidget->msg_queue_regular_padding_x;

   p_dispwidget->msg_queue_task_hourglass_x              = p_dispwidget->msg_queue_rect_start_x - p_dispwidget->msg_queue_icon_size_x;

   p_dispwidget->generic_message_height    = p_dispwidget->gfx_widget_fonts.regular.line_height * 2.0f;

   p_dispwidget->msg_queue_default_rect_width_menu_alive = p_dispwidget
      ->gfx_widget_fonts.msg_queue.glyph_width * 40.0f;
   p_dispwidget->msg_queue_default_rect_width            = p_dispwidget->last_video_width 
      - p_dispwidget->msg_queue_regular_text_start - (2 * p_dispwidget->simple_widget_padding);

#ifdef HAVE_MENU
   p_dispwidget->load_content_animation_icon_size_initial = 
      LOAD_CONTENT_ANIMATION_INITIAL_ICON_SIZE * 
      p_dispwidget->last_scale_factor;
   p_dispwidget->load_content_animation_icon_size_target  = 
      LOAD_CONTENT_ANIMATION_TARGET_ICON_SIZE  * 
      p_dispwidget->last_scale_factor;
#endif

   p_dispwidget->divider_width_1px    = 1;
   if (p_dispwidget->last_scale_factor > 1.0f)
      p_dispwidget->divider_width_1px = (unsigned)(p_dispwidget->last_scale_factor + 0.5f);

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->layout)
         widget->layout(p_dispwidget,
               is_threaded, dir_assets, font_path);
   }
}

static void gfx_widgets_context_reset(
      dispgfx_widget_t *p_dispwidget,
      bool is_threaded,
      unsigned width, unsigned height, bool fullscreen,
      const char *dir_assets, char *font_path)
{
   size_t i;
   char xmb_path[PATH_MAX_LENGTH];
   char monochrome_png_path[PATH_MAX_LENGTH];
   char gfx_widgets_path[PATH_MAX_LENGTH];
   char theme_path[PATH_MAX_LENGTH];

   xmb_path[0]            = '\0';
   monochrome_png_path[0] = '\0';
   gfx_widgets_path[0]    = '\0';
   theme_path[0]          = '\0';

   /* Textures paths */
   fill_pathname_join(
      gfx_widgets_path,
      dir_assets,
      "menu_widgets",
      sizeof(gfx_widgets_path)
   );

   fill_pathname_join(
      xmb_path,
      dir_assets,
      "xmb",
      sizeof(xmb_path)
   );

   /* Monochrome */
   fill_pathname_join(
      theme_path,
      xmb_path,
      "monochrome",
      sizeof(theme_path)
   );

   fill_pathname_join(
      monochrome_png_path,
      theme_path,
      "png",
      sizeof(monochrome_png_path)
   );

   /* Load textures */
   /* Icons */
   for (i = 0; i < MENU_WIDGETS_ICON_LAST; i++)
   {
      gfx_display_reset_textures_list(
            gfx_widgets_icons_names[i],
            monochrome_png_path,
            &p_dispwidget->gfx_widgets_icons_textures[i],
            TEXTURE_FILTER_MIPMAP_LINEAR,
            NULL,
            NULL);
   }

   /* Message queue */
   gfx_display_reset_textures_list(
         "msg_queue_icon.png",
         gfx_widgets_path,
         &p_dispwidget->msg_queue_icon,
         TEXTURE_FILTER_LINEAR,
         NULL,
         NULL);
   gfx_display_reset_textures_list(
         "msg_queue_icon_outline.png",
         gfx_widgets_path,
         &p_dispwidget->msg_queue_icon_outline,
         TEXTURE_FILTER_LINEAR,
         NULL,
         NULL);
   gfx_display_reset_textures_list(
         "msg_queue_icon_rect.png",
         gfx_widgets_path,
         &p_dispwidget->msg_queue_icon_rect,
         TEXTURE_FILTER_NEAREST,
         NULL,
         NULL);

   p_dispwidget->msg_queue_has_icons = 
      p_dispwidget->msg_queue_icon         && 
      p_dispwidget->msg_queue_icon_outline &&
      p_dispwidget->msg_queue_icon_rect;

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->context_reset)
         widget->context_reset(is_threaded, width, height,
               fullscreen, dir_assets, font_path,
               monochrome_png_path, gfx_widgets_path);
   }

   /* Update scaling/dimensions */
   p_dispwidget->last_video_width  = width;
   p_dispwidget->last_video_height = height;
   p_dispwidget->last_scale_factor = (
         gfx_display_get_driver_id() == MENU_DRIVER_ID_XMB) ?
         gfx_display_get_widget_pixel_scale(p_dispwidget->last_video_width,
               p_dispwidget->last_video_height, fullscreen) :
               gfx_display_get_widget_dpi_scale(
                     p_dispwidget->last_video_width,
                     p_dispwidget->last_video_height,
                     fullscreen);

   gfx_widgets_layout(p_dispwidget,
         is_threaded, dir_assets, font_path);
   video_driver_monitor_reset();
}

static void INLINE gfx_widgets_font_free(gfx_widget_font_data_t *font_data)
{
   if (font_data->font)
      gfx_display_font_free(font_data->font);

   font_data->font        = NULL;
   font_data->usage_count = 0;
}

static void gfx_widgets_context_destroy(dispgfx_widget_t *p_dispwidget)
{
   size_t i;

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->context_destroy)
         widget->context_destroy();
   }

   /* TODO: Dismiss onscreen notifications that have been freed */

   /* Textures */
   for (i = 0; i < MENU_WIDGETS_ICON_LAST; i++)
      video_driver_texture_unload(&p_dispwidget->gfx_widgets_icons_textures[i]);

   video_driver_texture_unload(&p_dispwidget->msg_queue_icon);
   video_driver_texture_unload(&p_dispwidget->msg_queue_icon_outline);
   video_driver_texture_unload(&p_dispwidget->msg_queue_icon_rect);

   p_dispwidget->msg_queue_icon         = 0;
   p_dispwidget->msg_queue_icon_outline = 0;
   p_dispwidget->msg_queue_icon_rect    = 0;

   /* Fonts */
   gfx_widgets_font_free(&p_dispwidget->gfx_widget_fonts.regular);
   gfx_widgets_font_free(&p_dispwidget->gfx_widget_fonts.bold);
   gfx_widgets_font_free(&p_dispwidget->gfx_widget_fonts.msg_queue);
}

#ifdef HAVE_CHEEVOS
static void gfx_widgets_achievement_free_current(dispgfx_widget_t *p_dispwidget)
{
   if (p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].title)
   {
      free(p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_read_index].title);
      p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].title = NULL;
   }

   if (p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].badge)
   {
      video_driver_texture_unload(&p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_read_index].badge);
      p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].badge = 0;
   }

   p_dispwidget->cheevo_popup_queue_read_index = (
         p_dispwidget->cheevo_popup_queue_read_index + 1) % CHEEVO_QUEUE_SIZE;
}

static void gfx_widgets_achievement_next(void* userdata)
{
   dispgfx_widget_t *p_dispwidget = (dispgfx_widget_t*)dispwidget_get_ptr();
   SLOCK_LOCK(p_dispwidget->cheevo_popup_queue_lock);

   gfx_widgets_achievement_free_current(p_dispwidget);

   /* start the next popup (if present) */
   if (p_dispwidget->cheevo_popup_queue[
         p_dispwidget->cheevo_popup_queue_read_index].title)
      gfx_widgets_start_achievement_notification(p_dispwidget);

   SLOCK_UNLOCK(p_dispwidget->cheevo_popup_queue_lock);
}
#endif

static void gfx_widgets_free(dispgfx_widget_t *p_dispwidget)
{
   size_t i;

   p_dispwidget->widgets_inited     = false;
   p_dispwidget->widgets_active     = false;

   for (i = 0; i < ARRAY_SIZE(widgets); i++)
   {
      const gfx_widget_t* widget = widgets[i];

      if (widget->free)
         widget->free();
   }

   /* Kill all running animations */
   gfx_animation_kill_by_tag(
         &p_dispwidget->gfx_widgets_generic_tag);

   /* Purge everything from the fifo */
   if (p_dispwidget->msg_queue)
   {
      while (fifo_read_avail(p_dispwidget->msg_queue) > 0)
      {
         menu_widget_msg_t *msg_widget;

         fifo_read(p_dispwidget->msg_queue,
               &msg_widget, sizeof(msg_widget));

         gfx_widgets_msg_queue_free(p_dispwidget, msg_widget, false);
         free(msg_widget);
      }

      fifo_free(p_dispwidget->msg_queue);
   }
   p_dispwidget->msg_queue = NULL;

   /* Purge everything from the list */
   if (p_dispwidget->current_msgs)
   {
      for (i = 0; i < p_dispwidget->current_msgs->size; i++)
      {
         menu_widget_msg_t *msg = (menu_widget_msg_t*)
            p_dispwidget->current_msgs->list[i].userdata;

         gfx_widgets_msg_queue_free(p_dispwidget, msg, false);
      }
      file_list_free(p_dispwidget->current_msgs);
   }
   p_dispwidget->current_msgs = NULL;

   p_dispwidget->msg_queue_tasks_count = 0;

#ifdef HAVE_CHEEVOS
   /* Achievement notification */
   if (p_dispwidget->cheevo_popup_queue_read_index >= 0)
   {
      SLOCK_LOCK(p_dispwidget->cheevo_popup_queue_lock);

      while (p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_read_index].title)
         gfx_widgets_achievement_free_current(p_dispwidget);

      SLOCK_UNLOCK(p_dispwidget->cheevo_popup_queue_lock);
   }
#endif

   /* Font */
   video_coord_array_free(
         &p_dispwidget->gfx_widget_fonts.regular.raster_block.carr);
   video_coord_array_free(
         &p_dispwidget->gfx_widget_fonts.bold.raster_block.carr);
   video_coord_array_free(
         &p_dispwidget->gfx_widget_fonts.msg_queue.raster_block.carr);

   font_driver_bind_block(NULL, NULL);
}

bool gfx_widgets_set_fps_text(const char *new_fps_text)
{
   dispgfx_widget_t *p_dispwidget = (dispgfx_widget_t*)dispwidget_get_ptr();
   if (!p_dispwidget->widgets_active)
      return false;

   strlcpy(p_dispwidget->gfx_widgets_fps_text,
         new_fps_text, sizeof(p_dispwidget->gfx_widgets_fps_text));

   return true;
}

#ifdef HAVE_TRANSLATE
int gfx_widgets_ai_service_overlay_get_state(void)
{
   dispgfx_widget_t *p_dispwidget = (dispgfx_widget_t*)dispwidget_get_ptr();
   return p_dispwidget->ai_service_overlay_state;
}

bool gfx_widgets_ai_service_overlay_set_state(int state)
{
   dispgfx_widget_t *p_dispwidget         = (dispgfx_widget_t*)dispwidget_get_ptr();
   p_dispwidget->ai_service_overlay_state = state;
   return true;
}

bool gfx_widgets_ai_service_overlay_load(
        char* buffer, unsigned buffer_len,
        enum image_type_enum image_type)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (p_dispwidget->ai_service_overlay_state == 0)
   {
      bool res = gfx_display_reset_textures_list_buffer(
               &p_dispwidget->ai_service_overlay_texture, 
               TEXTURE_FILTER_MIPMAP_LINEAR, 
               (void *) buffer, buffer_len, image_type,
               &p_dispwidget->ai_service_overlay_width,
               &p_dispwidget->ai_service_overlay_height);
      if (res)
         p_dispwidget->ai_service_overlay_state = 1;
      return res;
   }
   return true;
}

void gfx_widgets_ai_service_overlay_unload(void)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   if (p_dispwidget->ai_service_overlay_state == 1)
   {
      video_driver_texture_unload(&p_dispwidget->ai_service_overlay_texture);
      p_dispwidget->ai_service_overlay_texture = 0;
      p_dispwidget->ai_service_overlay_state   = 0;
   }
}
#endif

#ifdef HAVE_MENU
static void gfx_widgets_end_load_content_animation(void *userdata)
{
#if 0
   task_load_content_resume(); /* TODO: Restore that */
#endif
}

void gfx_widgets_cleanup_load_content_animation(void)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   p_dispwidget->load_content_animation_running = false;
   if (p_dispwidget->load_content_animation_content_name)
      free(p_dispwidget->load_content_animation_content_name);
   p_dispwidget->load_content_animation_content_name = NULL;
}
#endif

void gfx_widgets_start_load_content_animation(
      const char *content_name, bool remove_extension)
{
#ifdef HAVE_MENU
   /* TODO: finish the animation based on design, correct all timings */
   int i;
   gfx_animation_ctx_entry_t entry;
   gfx_timer_ctx_entry_t timer_entry;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   float icon_color[16]             = 
      COLOR_HEX_TO_FLOAT(0x0473C9, 1.0f); /* TODO: random color */
   unsigned timing                  = 0;

   if (!p_dispwidget->widgets_active)
      return;

   /* Prepare data */
   p_dispwidget->load_content_animation_icon         = 0;

   /* Abort animation if we don't have an icon */
   if (!menu_driver_get_load_content_animation_data(
            &p_dispwidget->load_content_animation_icon,
      &p_dispwidget->load_content_animation_playlist_name) || 
         !p_dispwidget->load_content_animation_icon)
   {
      gfx_widgets_end_load_content_animation(NULL);
      return;
   }

   p_dispwidget->load_content_animation_content_name = strdup(content_name);

   if (remove_extension)
      path_remove_extension(p_dispwidget->load_content_animation_content_name);

   /* Reset animation state */
   p_dispwidget->load_content_animation_icon_size          = p_dispwidget->load_content_animation_icon_size_initial;
   p_dispwidget->load_content_animation_icon_alpha         = 0.0f;
   p_dispwidget->load_content_animation_fade_alpha         = 0.0f;
   p_dispwidget->load_content_animation_final_fade_alpha   = 0.0f;

   memcpy(p_dispwidget->load_content_animation_icon_color, icon_color, sizeof(p_dispwidget->load_content_animation_icon_color));

   /* Setup the animation */
   entry.cb             = NULL;
   entry.easing_enum    = EASING_OUT_QUAD;
   entry.tag            = p_dispwidget->gfx_widgets_generic_tag;
   entry.userdata       = NULL;

   /* Stage one: icon animation */
   /* Position */
   entry.duration       = ANIMATION_LOAD_CONTENT_DURATION;
   entry.subject        = &p_dispwidget->load_content_animation_icon_size;
   entry.target_value   = p_dispwidget->load_content_animation_icon_size_target;

   gfx_animation_push(&entry);

   /* Alpha */
   entry.subject        = &p_dispwidget->load_content_animation_icon_alpha;
   entry.target_value   = 1.0f;

   gfx_animation_push(&entry);
   timing += entry.duration;

   /* Stage two: backdrop + text */
   entry.duration       = ANIMATION_LOAD_CONTENT_DURATION*1.5;
   entry.subject        = &p_dispwidget->load_content_animation_fade_alpha;
   entry.target_value   = 1.0f;

   gfx_animation_push_delayed(timing, &entry);
   timing += entry.duration;

   /* Stage three: wait then color transition */
   timing += ANIMATION_LOAD_CONTENT_DURATION*1.5;

   entry.duration = ANIMATION_LOAD_CONTENT_DURATION*3;

   for (i = 0; i < 16; i++)
   {
      if (i == 3 || i == 7 || i == 11 || i == 15)
         continue;

      entry.subject        = &p_dispwidget->load_content_animation_icon_color[i];
      entry.target_value   = gfx_widgets_pure_white[i];

      gfx_animation_push_delayed(timing, &entry);
   }

   timing += entry.duration;

   /* Stage four: wait then make everything disappear */
   timing += ANIMATION_LOAD_CONTENT_DURATION*2;

   entry.duration       = ANIMATION_LOAD_CONTENT_DURATION*1.5;
   entry.subject        = 
      &p_dispwidget->load_content_animation_final_fade_alpha;
   entry.target_value   = 1.0f;

   gfx_animation_push_delayed(timing, &entry);
   timing += entry.duration;

   /* Setup end */
   timer_entry.cb       = gfx_widgets_end_load_content_animation;
   timer_entry.duration = timing;
   timer_entry.userdata = NULL;

   gfx_timer_start(
         &p_dispwidget->load_content_animation_end_timer,
         &timer_entry);

   /* Draw all the things */
   p_dispwidget->load_content_animation_running = true;
#endif
}

#ifdef HAVE_CHEEVOS
static void gfx_widgets_achievement_dismiss(void *userdata)
{
   gfx_animation_ctx_entry_t entry;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   /* Slide up animation */
   entry.cb             = gfx_widgets_achievement_next;
   entry.duration       = MSG_QUEUE_ANIMATION_DURATION;
   entry.easing_enum    = EASING_OUT_QUAD;
   entry.subject        = &p_dispwidget->cheevo_y;
   entry.tag            = p_dispwidget->gfx_widgets_generic_tag;
   entry.target_value   = (float)(-(int)(p_dispwidget->cheevo_height));
   entry.userdata       = NULL;

   gfx_animation_push(&entry);
}

static void gfx_widgets_achievement_fold(void *userdata)
{
   gfx_animation_ctx_entry_t entry;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   /* Fold */
   entry.cb             = gfx_widgets_achievement_dismiss;
   entry.duration       = MSG_QUEUE_ANIMATION_DURATION;
   entry.easing_enum    = EASING_OUT_QUAD;
   entry.subject        = &p_dispwidget->cheevo_unfold;
   entry.tag            = p_dispwidget->gfx_widgets_generic_tag;
   entry.target_value   = 0.0f;
   entry.userdata       = NULL;

   gfx_animation_push(&entry);
}

static void gfx_widgets_achievement_unfold(void *userdata)
{
   gfx_timer_ctx_entry_t timer;
   gfx_animation_ctx_entry_t entry;
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();

   /* Unfold */
   entry.cb             = NULL;
   entry.duration       = MSG_QUEUE_ANIMATION_DURATION;
   entry.easing_enum    = EASING_OUT_QUAD;
   entry.subject        = &p_dispwidget->cheevo_unfold;
   entry.tag            = p_dispwidget->gfx_widgets_generic_tag;
   entry.target_value   = 1.0f;
   entry.userdata       = NULL;

   gfx_animation_push(&entry);

   /* Wait before dismissing */
   timer.cb       = gfx_widgets_achievement_fold;
   timer.duration = MSG_QUEUE_ANIMATION_DURATION + CHEEVO_NOTIFICATION_DURATION;
   timer.userdata = NULL;

   gfx_timer_start(&p_dispwidget->cheevo_timer, &timer);
}

static void gfx_widgets_start_achievement_notification(
      dispgfx_widget_t *p_dispwidget)
{
   gfx_animation_ctx_entry_t entry;
   p_dispwidget->cheevo_height      = 
      p_dispwidget->gfx_widget_fonts.regular.line_height * 4;
   p_dispwidget->cheevo_width       = MAX(
         font_driver_get_message_width(
            p_dispwidget->gfx_widget_fonts.regular.font,
            msg_hash_to_str(MSG_ACHIEVEMENT_UNLOCKED), 0, 1),
         font_driver_get_message_width(
            p_dispwidget->gfx_widget_fonts.regular.font,
            p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_read_index].title, 0, 1)
   );
   p_dispwidget->cheevo_width      += p_dispwidget->simple_widget_padding * 2;
   p_dispwidget->cheevo_y           = (float)(-(int)p_dispwidget->cheevo_height);
   p_dispwidget->cheevo_unfold      = 0.0f;

   /* Slide down animation */
   entry.cb                         = gfx_widgets_achievement_unfold;
   entry.duration                   = MSG_QUEUE_ANIMATION_DURATION;
   entry.easing_enum                = EASING_OUT_QUAD;
   entry.subject                    = &p_dispwidget->cheevo_y;
   entry.tag                        = p_dispwidget->gfx_widgets_generic_tag;
   entry.target_value               = 0.0f;
   entry.userdata                   = NULL;

   gfx_animation_push(&entry);
}

void gfx_widgets_push_achievement(const char *title, const char *badge)
{
   dispgfx_widget_t *p_dispwidget   = (dispgfx_widget_t*)dispwidget_get_ptr();
   int           start_notification = 1;

   if (!p_dispwidget->widgets_active)
      return;

   if (p_dispwidget->cheevo_popup_queue_read_index < 0)
   {
      /* queue uninitialized */
      memset(&p_dispwidget->cheevo_popup_queue, 0,
            sizeof(p_dispwidget->cheevo_popup_queue));
      p_dispwidget->cheevo_popup_queue_read_index = 0;

#ifdef HAVE_THREADS
      p_dispwidget->cheevo_popup_queue_lock = slock_new();
#endif
   }

   SLOCK_LOCK(p_dispwidget->cheevo_popup_queue_lock);

   if (p_dispwidget->cheevo_popup_queue_write_index 
         == p_dispwidget->cheevo_popup_queue_read_index)
   {
      if (p_dispwidget->cheevo_popup_queue[
            p_dispwidget->cheevo_popup_queue_write_index].title)
      {
         /* queue full */
         SLOCK_UNLOCK(p_dispwidget->cheevo_popup_queue_lock);
         return;
      }

      /* queue empty */
   }
   else
      start_notification = 0; /* notification already being displayed */

   p_dispwidget->cheevo_popup_queue[p_dispwidget->cheevo_popup_queue_write_index].badge = cheevos_get_badge_texture(badge, 0);
   p_dispwidget->cheevo_popup_queue[p_dispwidget->cheevo_popup_queue_write_index].title = strdup(title);

   p_dispwidget->cheevo_popup_queue_write_index = (p_dispwidget->cheevo_popup_queue_write_index + 1) % CHEEVO_QUEUE_SIZE;

   if (start_notification)
      gfx_widgets_start_achievement_notification(p_dispwidget);

   SLOCK_UNLOCK(p_dispwidget->cheevo_popup_queue_lock);
}
#endif
