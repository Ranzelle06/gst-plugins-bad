/* GStreamer
 *
 * unit test for audiomixer
 *
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2013 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstconsistencychecker.h>
#include <gst/base/gstbasesrc.h>

static GMainLoop *main_loop;

/* make sure downstream gets a CAPS event before buffers are sent */
GST_START_TEST (test_caps)
{
  GstElement *pipeline, *src, *audiomixer, *sink;
  GstStateChangeReturn state_res;
  GstCaps *caps;
  GstPad *pad;

  /* build pipeline */
  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src, "wave", 4, NULL);  /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), src, audiomixer, sink, NULL);

  fail_unless (gst_element_link_many (src, audiomixer, sink, NULL));

  /* prepare playing */
  state_res = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless_equals_int (state_res, GST_STATE_CHANGE_ASYNC);

  /* wait for preroll */
  state_res = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (state_res, GST_STATE_CHANGE_SUCCESS);

  /* check caps on fakesink */
  pad = gst_element_get_static_pad (sink, "sink");
  caps = gst_pad_get_current_caps (pad);
  fail_unless (caps != NULL);
  gst_caps_unref (caps);
  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* check that caps set on the property are honoured */
GST_START_TEST (test_filter_caps)
{
  GstElement *pipeline, *src, *audiomixer, *sink;
  GstStateChangeReturn state_res;
  GstCaps *filter_caps, *caps;
  GstPad *pad;

  filter_caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "F32LE",
      "layout", G_TYPE_STRING, "interleaved",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 1, NULL);

  /* build pipeline */
  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (src, "wave", 4, NULL);  /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", NULL);
  g_object_set (audiomixer, "caps", filter_caps, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), src, audiomixer, sink, NULL);

  fail_unless (gst_element_link_many (src, audiomixer, sink, NULL));

  /* prepare playing */
  state_res = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless_equals_int (state_res, GST_STATE_CHANGE_ASYNC);

  /* wait for preroll */
  state_res = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (state_res, GST_STATE_CHANGE_SUCCESS);

  /* check caps on fakesink */
  pad = gst_element_get_static_pad (sink, "sink");
  caps = gst_pad_get_current_caps (pad);
  fail_unless (caps != NULL);
  GST_INFO_OBJECT (pipeline, "received caps: %" GST_PTR_FORMAT, caps);
  fail_unless (gst_caps_is_equal_fixed (caps, filter_caps));
  gst_caps_unref (caps);
  gst_object_unref (pad);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  gst_caps_unref (filter_caps);
}

GST_END_TEST;

static gboolean
set_playing (GstElement * element)
{
  GstStateChangeReturn state_res;

  state_res = gst_element_set_state (element, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  return FALSE;
}

static void
message_received (GstBus * bus, GstMessage * message, GstPipeline * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (main_loop);
      break;
    case GST_MESSAGE_WARNING:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_warning (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (main_loop);
      break;
    }
    default:
      break;
  }
}


static GstFormat format = GST_FORMAT_UNDEFINED;
static gint64 position = -1;

static void
test_event_message_received (GstBus * bus, GstMessage * message,
    GstPipeline * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_SEGMENT_DONE:
      gst_message_parse_segment_done (message, &format, &position);
      GST_INFO ("received segment_done : %" G_GINT64_FORMAT, position);
      g_main_loop_quit (main_loop);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


GST_START_TEST (test_event)
{
  GstElement *bin, *src1, *src2, *audiomixer, *sink;
  GstBus *bus;
  GstEvent *seek_event;
  GstStateChangeReturn state_res;
  gboolean res;
  GstPad *srcpad, *sinkpad;
  GstStreamConsistency *chk_1, *chk_2, *chk_3;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, audiomixer, sink, NULL);

  res = gst_element_link (src1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  chk_3 = gst_consistency_checker_new (srcpad);
  gst_object_unref (srcpad);

  /* create consistency checkers for the pads */
  srcpad = gst_element_get_static_pad (src1, "src");
  chk_1 = gst_consistency_checker_new (srcpad);
  sinkpad = gst_pad_get_peer (srcpad);
  gst_consistency_checker_add_pad (chk_3, sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  srcpad = gst_element_get_static_pad (src2, "src");
  chk_2 = gst_consistency_checker_new (srcpad);
  sinkpad = gst_pad_get_peer (srcpad);
  gst_consistency_checker_add_pad (chk_3, sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);

  format = GST_FORMAT_UNDEFINED;
  position = -1;

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done",
      (GCallback) test_event_message_received, bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  GST_INFO ("starting test");

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res = gst_element_get_state (bin, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  res = gst_element_send_event (bin, seek_event);
  fail_unless (res == TRUE, NULL);

  /* run pipeline */
  g_idle_add ((GSourceFunc) set_playing, bin);

  GST_INFO ("running main loop");
  g_main_loop_run (main_loop);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  ck_assert_int_eq (position, 2 * GST_SECOND);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_consistency_checker_free (chk_1);
  gst_consistency_checker_free (chk_2);
  gst_consistency_checker_free (chk_3);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

static guint play_count = 0;
static GstEvent *play_seek_event = NULL;

static void
test_play_twice_message_received (GstBus * bus, GstMessage * message,
    GstPipeline * bin)
{
  gboolean res;
  GstStateChangeReturn state_res;

  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_SEGMENT_DONE:
      play_count++;
      if (play_count == 1) {
        state_res = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
        ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

        /* prepare playing again */
        state_res = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
        ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

        /* wait for completion */
        state_res =
            gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
            GST_CLOCK_TIME_NONE);
        ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

        res = gst_element_send_event (GST_ELEMENT (bin),
            gst_event_ref (play_seek_event));
        fail_unless (res == TRUE, NULL);

        state_res =
            gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
        ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);
      } else {
        g_main_loop_quit (main_loop);
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


GST_START_TEST (test_play_twice)
{
  GstElement *bin, *src1, *src2, *audiomixer, *sink;
  GstBus *bus;
  gboolean res;
  GstStateChangeReturn state_res;
  GstPad *srcpad;
  GstStreamConsistency *consist;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, audiomixer, sink, NULL);

  res = gst_element_link (src1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  consist = gst_consistency_checker_new (srcpad);
  gst_object_unref (srcpad);

  play_seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);

  play_count = 0;

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done",
      (GCallback) test_play_twice_message_received, bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  GST_INFO ("starting test");

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  res = gst_element_send_event (bin, gst_event_ref (play_seek_event));
  fail_unless (res == TRUE, NULL);

  GST_INFO ("seeked");

  /* run pipeline */
  g_idle_add ((GSourceFunc) set_playing, bin);

  g_main_loop_run (main_loop);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  ck_assert_int_eq (play_count, 2);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_consistency_checker_free (consist);
  gst_event_ref (play_seek_event);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_play_twice_then_add_and_play_again)
{
  GstElement *bin, *src1, *src2, *src3, *audiomixer, *sink;
  GstBus *bus;
  gboolean res;
  GstStateChangeReturn state_res;
  gint i;
  GstPad *srcpad;
  GstStreamConsistency *consist;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, audiomixer, sink, NULL);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  consist = gst_consistency_checker_new (srcpad);
  gst_object_unref (srcpad);

  res = gst_element_link (src1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  play_seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done",
      (GCallback) test_play_twice_message_received, bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  /* run it twice */
  for (i = 0; i < 2; i++) {
    play_count = 0;

    GST_INFO ("starting test-loop %d", i);

    /* prepare playing */
    state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    /* wait for completion */
    state_res =
        gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
        GST_CLOCK_TIME_NONE);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    res = gst_element_send_event (bin, gst_event_ref (play_seek_event));
    fail_unless (res == TRUE, NULL);

    GST_INFO ("seeked");

    /* run pipeline */
    g_idle_add ((GSourceFunc) set_playing, bin);

    g_main_loop_run (main_loop);

    state_res = gst_element_set_state (bin, GST_STATE_READY);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    ck_assert_int_eq (play_count, 2);

    /* plug another source */
    if (i == 0) {
      src3 = gst_element_factory_make ("audiotestsrc", "src3");
      g_object_set (src3, "wave", 4, NULL);     /* silence */
      gst_bin_add (GST_BIN (bin), src3);

      res = gst_element_link (src3, audiomixer);
      fail_unless (res == TRUE, NULL);
    }

    gst_consistency_checker_reset (consist);
  }

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_event_ref (play_seek_event);
  gst_consistency_checker_free (consist);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;


static void
test_live_seeking_eos_message_received (GstBus * bus, GstMessage * message,
    GstPipeline * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (main_loop);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static GstElement *
test_live_seeking_try_audiosrc (const gchar * factory_name)
{
  GstElement *src;
  GstStateChangeReturn state_res;

  if (!(src = gst_element_factory_make (factory_name, NULL))) {
    GST_INFO ("can't make '%s', skipping", factory_name);
    return NULL;
  }

  /* Test that the audio source can get to ready, else skip */
  state_res = gst_element_set_state (src, GST_STATE_READY);
  gst_element_set_state (src, GST_STATE_NULL);

  if (state_res == GST_STATE_CHANGE_FAILURE) {
    GST_INFO_OBJECT (src, "can't go to ready, skipping");
    gst_object_unref (src);
    return NULL;
  }

  return src;
}

/* test failing seeks on live-sources */
GST_START_TEST (test_live_seeking)
{
  GstElement *bin, *src1 = NULL, *src2, *ac1, *ac2, *audiomixer, *sink;
  GstBus *bus;
  gboolean res;
  GstPad *srcpad;
  gint i;
  GstStateChangeReturn state_res;
  GstStreamConsistency *consist;
  /* don't use autoaudiosrc, as then we can't set anything here */
  const gchar *audio_src_factories[] = {
    "alsasrc",
    "pulseaudiosrc"
  };

  GST_INFO ("preparing test");
  main_loop = NULL;
  play_seek_event = NULL;

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  for (i = 0; (i < G_N_ELEMENTS (audio_src_factories) && src1 == NULL); i++) {
    src1 = test_live_seeking_try_audiosrc (audio_src_factories[i]);
  }
  if (!src1) {
    /* normal audiosources behave differently than audiotestsrc */
    src1 = gst_element_factory_make ("audiotestsrc", "src1");
    g_object_set (src1, "wave", 4, "is-live", TRUE, NULL);      /* silence */
  } else {
    /* live sources ignore seeks, force eos after 2 sec (4 buffers half second
     * each)
     */
    g_object_set (src1, "num-buffers", 4, "blocksize", 44100, NULL);
  }

  ac1 = gst_element_factory_make ("audioconvert", "ac1");
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  ac2 = gst_element_factory_make ("audioconvert", "ac2");
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, ac1, src2, ac2, audiomixer, sink,
      NULL);

  res = gst_element_link (src1, ac1);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (ac1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, ac2);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (ac2, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  play_seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 2 * GST_SECOND);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos",
      (GCallback) test_live_seeking_eos_message_received, bin);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  consist = gst_consistency_checker_new (srcpad);
  gst_object_unref (srcpad);

  GST_INFO ("starting test");

  /* run it twice */
  for (i = 0; i < 2; i++) {

    GST_INFO ("starting test-loop %d", i);

    /* prepare playing */
    state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    /* wait for completion */
    state_res =
        gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
        GST_CLOCK_TIME_NONE);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    res = gst_element_send_event (bin, gst_event_ref (play_seek_event));
    fail_unless (res == TRUE, NULL);

    GST_INFO ("seeked");

    /* run pipeline */
    g_idle_add ((GSourceFunc) set_playing, bin);

    GST_INFO ("playing");

    g_main_loop_run (main_loop);

    state_res = gst_element_set_state (bin, GST_STATE_NULL);
    ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

    gst_consistency_checker_reset (consist);
  }

  /* cleanup */
  GST_INFO ("cleaning up");
  gst_consistency_checker_free (consist);
  if (main_loop)
    g_main_loop_unref (main_loop);
  if (play_seek_event)
    gst_event_unref (play_seek_event);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

/* check if adding pads work as expected */
GST_START_TEST (test_add_pad)
{
  GstElement *bin, *src1, *src2, *audiomixer, *sink;
  GstBus *bus;
  GstPad *srcpad;
  gboolean res;
  GstStateChangeReturn state_res;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "num-buffers", 4, NULL);
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  /* one buffer less, we connect with 1 buffer of delay */
  g_object_set (src2, "num-buffers", 3, NULL);
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, audiomixer, sink, NULL);

  res = gst_element_link (src1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  gst_object_unref (srcpad);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done", (GCallback) message_received,
      bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  GST_INFO ("starting test");

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* add other element */
  gst_bin_add_many (GST_BIN (bin), src2, NULL);

  /* now link the second element */
  res = gst_element_link (src2, audiomixer);
  fail_unless (res == TRUE, NULL);

  /* set to PAUSED as well */
  state_res = gst_element_set_state (src2, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* now play all */
  g_idle_add ((GSourceFunc) set_playing, bin);

  g_main_loop_run (main_loop);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

/* check if removing pads work as expected */
GST_START_TEST (test_remove_pad)
{
  GstElement *bin, *src, *audiomixer, *sink;
  GstBus *bus;
  GstPad *pad, *srcpad;
  gboolean res;
  GstStateChangeReturn state_res;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src = gst_element_factory_make ("audiotestsrc", "src");
  g_object_set (src, "num-buffers", 4, NULL);
  g_object_set (src, "wave", 4, NULL);
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src, audiomixer, sink, NULL);

  res = gst_element_link (src, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  /* create an unconnected sinkpad in audiomixer */
  pad = gst_element_get_request_pad (audiomixer, "sink_%u");
  fail_if (pad == NULL, NULL);

  srcpad = gst_element_get_static_pad (audiomixer, "src");
  gst_object_unref (srcpad);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done", (GCallback) message_received,
      bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  GST_INFO ("starting test");

  /* prepare playing, this will not preroll as audiomixer is waiting
   * on the unconnected sinkpad. */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion for one second, will return ASYNC */
  state_res = gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_SECOND);
  ck_assert_int_eq (state_res, GST_STATE_CHANGE_ASYNC);

  /* get rid of the pad now, audiomixer should stop waiting on it and
   * continue the preroll */
  gst_element_release_request_pad (audiomixer, pad);
  gst_object_unref (pad);

  /* wait for completion, should work now */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* now play all */
  g_idle_add ((GSourceFunc) set_playing, bin);

  g_main_loop_run (main_loop);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (G_OBJECT (bus));
  gst_object_unref (G_OBJECT (bin));
}

GST_END_TEST;


static GstBuffer *handoff_buffer = NULL;
static void
handoff_buffer_cb (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  GST_DEBUG ("got buffer %p", buffer);
  gst_buffer_replace (&handoff_buffer, buffer);
}

/* check if clipping works as expected */
GST_START_TEST (test_clip)
{
  GstSegment segment;
  GstElement *bin, *audiomixer, *sink;
  GstBus *bus;
  GstPad *sinkpad;
  gboolean res;
  GstStateChangeReturn state_res;
  GstFlowReturn ret;
  GstEvent *event;
  GstBuffer *buffer;
  GstCaps *caps;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  /* just an audiomixer and a fakesink */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff_buffer_cb, NULL);
  gst_bin_add_many (GST_BIN (bin), audiomixer, sink, NULL);

  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  /* set to playing */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* create an unconnected sinkpad in audiomixer, should also automatically activate
   * the pad */
  sinkpad = gst_element_get_request_pad (audiomixer, "sink_%u");
  fail_if (sinkpad == NULL, NULL);

  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_simple ("audio/x-raw",
#if G_BYTE_ORDER == G_BIG_ENDIAN
      "format", G_TYPE_STRING, "S16BE",
#else
      "format", G_TYPE_STRING, "S16LE",
#endif
      "layout", G_TYPE_STRING, "interleaved",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 2, NULL);

  gst_pad_set_caps (sinkpad, caps);
  gst_caps_unref (caps);

  /* send segment to audiomixer */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = GST_SECOND;
  segment.stop = 2 * GST_SECOND;
  segment.time = 0;
  event = gst_event_new_segment (&segment);
  gst_pad_send_event (sinkpad, event);

  /* should be clipped and ok */
  buffer = gst_buffer_new_and_alloc (44100);
  GST_BUFFER_TIMESTAMP (buffer) = 0;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (sinkpad, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);
  fail_unless (handoff_buffer == NULL);

  /* should be partially clipped */
  buffer = gst_buffer_new_and_alloc (44100);
  GST_BUFFER_TIMESTAMP (buffer) = 900 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (sinkpad, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);
  fail_unless (handoff_buffer != NULL);
  gst_buffer_replace (&handoff_buffer, NULL);

  /* should not be clipped */
  buffer = gst_buffer_new_and_alloc (44100);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (sinkpad, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);
  fail_unless (handoff_buffer != NULL);
  gst_buffer_replace (&handoff_buffer, NULL);

  /* should be clipped and ok */
  buffer = gst_buffer_new_and_alloc (44100);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (sinkpad, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);
  fail_unless (handoff_buffer == NULL);

  gst_element_release_request_pad (audiomixer, sinkpad);
  gst_object_unref (sinkpad);
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_duration_is_max)
{
  GstElement *bin, *src[3], *audiomixer, *sink;
  GstStateChangeReturn state_res;
  GstFormat format = GST_FORMAT_TIME;
  gboolean res;
  gint64 duration;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");

  /* 3 sources, an audiomixer and a fakesink */
  src[0] = gst_element_factory_make ("audiotestsrc", NULL);
  src[1] = gst_element_factory_make ("audiotestsrc", NULL);
  src[2] = gst_element_factory_make ("audiotestsrc", NULL);
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src[0], src[1], src[2], audiomixer, sink,
      NULL);

  gst_element_link (src[0], audiomixer);
  gst_element_link (src[1], audiomixer);
  gst_element_link (src[2], audiomixer);
  gst_element_link (audiomixer, sink);

  /* irks, duration is reset on basesrc */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* set durations on src */
  GST_BASE_SRC (src[0])->segment.duration = 1000;
  GST_BASE_SRC (src[1])->segment.duration = 3000;
  GST_BASE_SRC (src[2])->segment.duration = 2000;

  /* set to playing */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_query_duration (GST_ELEMENT (bin), format, &duration);
  fail_unless (res, NULL);

  ck_assert_int_eq (duration, 3000);

  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_duration_unknown_overrides)
{
  GstElement *bin, *src[3], *audiomixer, *sink;
  GstStateChangeReturn state_res;
  GstFormat format = GST_FORMAT_TIME;
  gboolean res;
  gint64 duration;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");

  /* 3 sources, an audiomixer and a fakesink */
  src[0] = gst_element_factory_make ("audiotestsrc", NULL);
  src[1] = gst_element_factory_make ("audiotestsrc", NULL);
  src[2] = gst_element_factory_make ("audiotestsrc", NULL);
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src[0], src[1], src[2], audiomixer, sink,
      NULL);

  gst_element_link (src[0], audiomixer);
  gst_element_link (src[1], audiomixer);
  gst_element_link (src[2], audiomixer);
  gst_element_link (audiomixer, sink);

  /* irks, duration is reset on basesrc */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* set durations on src */
  GST_BASE_SRC (src[0])->segment.duration = GST_CLOCK_TIME_NONE;
  GST_BASE_SRC (src[1])->segment.duration = 3000;
  GST_BASE_SRC (src[2])->segment.duration = 2000;

  /* set to playing */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_query_duration (GST_ELEMENT (bin), format, &duration);
  fail_unless (res, NULL);

  ck_assert_int_eq (duration, GST_CLOCK_TIME_NONE);

  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bin);
}

GST_END_TEST;


static gboolean looped = FALSE;

static void
loop_segment_done (GstBus * bus, GstMessage * message, GstElement * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  if (looped) {
    g_main_loop_quit (main_loop);
  } else {
    GstEvent *seek_event;
    gboolean res;

    seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
        GST_SEEK_FLAG_SEGMENT,
        GST_SEEK_TYPE_SET, (GstClockTime) 0,
        GST_SEEK_TYPE_SET, (GstClockTime) 1 * GST_SECOND);

    res = gst_element_send_event (bin, seek_event);
    fail_unless (res == TRUE, NULL);
    looped = TRUE;
  }
}

GST_START_TEST (test_loop)
{
  GstElement *bin, *src1, *src2, *audiomixer, *sink;
  GstBus *bus;
  GstEvent *seek_event;
  GstStateChangeReturn state_res;
  gboolean res;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, audiomixer, sink, NULL);

  res = gst_element_link (src1, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (src2, audiomixer);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  seek_event = gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, (GstClockTime) 0,
      GST_SEEK_TYPE_SET, (GstClockTime) 1 * GST_SECOND);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::segment-done",
      (GCallback) loop_segment_done, bin);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  GST_INFO ("starting test");

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res = gst_element_get_state (bin, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  res = gst_element_send_event (bin, seek_event);
  fail_unless (res == TRUE, NULL);

  /* run pipeline */
  g_idle_add ((GSourceFunc) set_playing, bin);

  GST_INFO ("running main loop");
  g_main_loop_run (main_loop);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_flush_start_flush_stop)
{
  GstPadTemplate *sink_template;
  GstPad *tmppad, *sinkpad1, *sinkpad2, *audiomixer_src;
  GstElement *pipeline, *src1, *src2, *audiomixer, *sink;

  GST_INFO ("preparing test");

  /* build pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  src1 = gst_element_factory_make ("audiotestsrc", "src1");
  g_object_set (src1, "wave", 4, NULL); /* silence */
  src2 = gst_element_factory_make ("audiotestsrc", "src2");
  g_object_set (src2, "wave", 4, NULL); /* silence */
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), src1, src2, audiomixer, sink, NULL);

  sink_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (audiomixer),
      "sink_%u");
  fail_unless (GST_IS_PAD_TEMPLATE (sink_template));
  sinkpad1 = gst_element_request_pad (audiomixer, sink_template, NULL, NULL);
  tmppad = gst_element_get_static_pad (src1, "src");
  gst_pad_link (tmppad, sinkpad1);
  gst_object_unref (tmppad);

  sinkpad2 = gst_element_request_pad (audiomixer, sink_template, NULL, NULL);
  tmppad = gst_element_get_static_pad (src2, "src");
  gst_pad_link (tmppad, sinkpad2);
  gst_object_unref (tmppad);

  gst_element_link (audiomixer, sink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (gst_element_get_state (pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_SUCCESS);

  audiomixer_src = gst_element_get_static_pad (audiomixer, "src");
  fail_if (GST_PAD_IS_FLUSHING (audiomixer_src));
  gst_pad_send_event (sinkpad1, gst_event_new_flush_start ());
  fail_unless (GST_PAD_IS_FLUSHING (audiomixer_src));
  gst_pad_send_event (sinkpad1, gst_event_new_flush_stop (TRUE));
  fail_if (GST_PAD_IS_FLUSHING (audiomixer_src));
  gst_object_unref (audiomixer_src);

  gst_element_release_request_pad (audiomixer, sinkpad1);
  gst_object_unref (sinkpad1);
  gst_element_release_request_pad (audiomixer, sinkpad2);
  gst_object_unref (sinkpad2);

  /* cleanup */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static void
handoff_buffer_collect_cb (GstElement * fakesink, GstBuffer * buffer,
    GstPad * pad, gpointer user_data)
{
  GList **received_buffers = user_data;

  GST_DEBUG ("got buffer %p", buffer);
  *received_buffers =
      g_list_append (*received_buffers, gst_buffer_ref (buffer));
}

typedef void (*SendBuffersFunction) (GstPad * pad1, GstPad * pad2);
typedef void (*CheckBuffersFunction) (GList * buffers);

static void
run_sync_test (SendBuffersFunction send_buffers,
    CheckBuffersFunction check_buffers)
{
  GstSegment segment;
  GstElement *bin, *audiomixer, *queue1, *queue2, *sink;
  GstBus *bus;
  GstPad *sinkpad1, *sinkpad2;
  GstPad *queue1_sinkpad, *queue2_sinkpad;
  GstPad *pad;
  gboolean res;
  GstStateChangeReturn state_res;
  GstEvent *event;
  GstCaps *caps;
  GList *received_buffers = NULL;

  GST_INFO ("preparing test");

  main_loop = g_main_loop_new (NULL, FALSE);

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  /* just an audiomixer and a fakesink */
  queue1 = gst_element_factory_make ("queue", "queue1");
  queue2 = gst_element_factory_make ("queue", "queue2");
  audiomixer = gst_element_factory_make ("audiomixer", "audiomixer");
  g_object_set (audiomixer, "blocksize", 500, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff_buffer_collect_cb,
      &received_buffers);
  gst_bin_add_many (GST_BIN (bin), queue1, queue2, audiomixer, sink, NULL);

  res = gst_element_link (audiomixer, sink);
  fail_unless (res == TRUE, NULL);

  /* set to paused */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* create an unconnected sinkpad in audiomixer, should also automatically activate
   * the pad */
  sinkpad1 = gst_element_get_request_pad (audiomixer, "sink_%u");
  fail_if (sinkpad1 == NULL, NULL);

  queue1_sinkpad = gst_element_get_static_pad (queue1, "sink");
  pad = gst_element_get_static_pad (queue1, "src");
  fail_unless (gst_pad_link (pad, sinkpad1) == GST_PAD_LINK_OK);
  gst_object_unref (pad);

  sinkpad2 = gst_element_get_request_pad (audiomixer, "sink_%u");
  fail_if (sinkpad2 == NULL, NULL);

  queue2_sinkpad = gst_element_get_static_pad (queue2, "sink");
  pad = gst_element_get_static_pad (queue2, "src");
  fail_unless (gst_pad_link (pad, sinkpad2) == GST_PAD_LINK_OK);
  gst_object_unref (pad);

  gst_pad_send_event (queue1_sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (queue2_sinkpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_simple ("audio/x-raw",
#if G_BYTE_ORDER == G_BIG_ENDIAN
      "format", G_TYPE_STRING, "S16BE",
#else
      "format", G_TYPE_STRING, "S16LE",
#endif
      "layout", G_TYPE_STRING, "interleaved",
      "rate", G_TYPE_INT, 1000, "channels", G_TYPE_INT, 1, NULL);

  gst_pad_set_caps (queue1_sinkpad, caps);
  gst_pad_set_caps (queue2_sinkpad, caps);
  gst_caps_unref (caps);

  /* send segment to audiomixer */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  gst_pad_send_event (queue1_sinkpad, gst_event_ref (event));
  gst_pad_send_event (queue2_sinkpad, event);

  /* Push buffers */
  send_buffers (queue1_sinkpad, queue2_sinkpad);

  /* Set PLAYING */
  g_idle_add ((GSourceFunc) set_playing, bin);

  /* Collect buffers and messages */
  g_main_loop_run (main_loop);

  /* Here we get once we got EOS, for errors we failed */

  check_buffers (received_buffers);

  g_list_free_full (received_buffers, (GDestroyNotify) gst_buffer_unref);

  gst_element_release_request_pad (audiomixer, sinkpad1);
  gst_object_unref (sinkpad1);
  gst_object_unref (queue1_sinkpad);
  gst_element_release_request_pad (audiomixer, sinkpad2);
  gst_object_unref (sinkpad2);
  gst_object_unref (queue2_sinkpad);
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
  g_main_loop_unref (main_loop);
}

static void
send_buffers_sync (GstPad * pad1, GstPad * pad2)
{
  GstBuffer *buffer;
  GstMapInfo map;
  GstFlowReturn ret;

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 1, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad1, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 1, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad1, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  gst_pad_send_event (pad1, gst_event_new_eos ());

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 2, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad2, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 2, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad2, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  gst_pad_send_event (pad2, gst_event_new_eos ());
}

static void
check_buffers_sync (GList * received_buffers)
{
  GstBuffer *buffer;
  GList *l;
  gint i;
  GstMapInfo map;

  /* Should have 8 * 0.5s buffers */
  fail_unless_equals_int (g_list_length (received_buffers), 8);
  for (i = 0, l = received_buffers; l; l = l->next, i++) {
    buffer = l->data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    if (i == 0 && GST_BUFFER_TIMESTAMP (buffer) == 0) {
      fail_unless (map.data[0] == 0);
      fail_unless (map.data[map.size - 1] == 0);
    } else if (i == 1 && GST_BUFFER_TIMESTAMP (buffer) == 500 * GST_MSECOND) {
      fail_unless (map.data[0] == 0);
      fail_unless (map.data[map.size - 1] == 0);
    } else if (i == 2 && GST_BUFFER_TIMESTAMP (buffer) == 1000 * GST_MSECOND) {
      fail_unless (map.data[0] == 1);
      fail_unless (map.data[map.size - 1] == 1);
    } else if (i == 3 && GST_BUFFER_TIMESTAMP (buffer) == 1500 * GST_MSECOND) {
      fail_unless (map.data[0] == 1);
      fail_unless (map.data[map.size - 1] == 1);
    } else if (i == 4 && GST_BUFFER_TIMESTAMP (buffer) == 2000 * GST_MSECOND) {
      fail_unless (map.data[0] == 3);
      fail_unless (map.data[map.size - 1] == 3);
    } else if (i == 5 && GST_BUFFER_TIMESTAMP (buffer) == 2500 * GST_MSECOND) {
      fail_unless (map.data[0] == 3);
      fail_unless (map.data[map.size - 1] == 3);
    } else if (i == 6 && GST_BUFFER_TIMESTAMP (buffer) == 3000 * GST_MSECOND) {
      fail_unless (map.data[0] == 2);
      fail_unless (map.data[map.size - 1] == 2);
    } else if (i == 7 && GST_BUFFER_TIMESTAMP (buffer) == 3500 * GST_MSECOND) {
      fail_unless (map.data[0] == 2);
      fail_unless (map.data[map.size - 1] == 2);
    } else {
      g_assert_not_reached ();
    }

    gst_buffer_unmap (buffer, &map);

  }
}

GST_START_TEST (test_sync)
{
  run_sync_test (send_buffers_sync, check_buffers_sync);
}

GST_END_TEST;

static void
send_buffers_sync_discont (GstPad * pad1, GstPad * pad2)
{
  GstBuffer *buffer;
  GstMapInfo map;
  GstFlowReturn ret;

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 1, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 1 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad1, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 1, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad1, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  gst_pad_send_event (pad1, gst_event_new_eos ());

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 2, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad2, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (2000);
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  memset (map.data, 2, map.size);
  gst_buffer_unmap (buffer, &map);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  GST_DEBUG ("pushing buffer %p", buffer);
  ret = gst_pad_chain (pad2, buffer);
  ck_assert_int_eq (ret, GST_FLOW_OK);

  gst_pad_send_event (pad2, gst_event_new_eos ());
}

static void
check_buffers_sync_discont (GList * received_buffers)
{
  GstBuffer *buffer;
  GList *l;
  gint i;
  GstMapInfo map;

  /* Should have 8 * 0.5s buffers */
  fail_unless_equals_int (g_list_length (received_buffers), 8);
  for (i = 0, l = received_buffers; l; l = l->next, i++) {
    buffer = l->data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    if (i == 0 && GST_BUFFER_TIMESTAMP (buffer) == 0) {
      fail_unless (map.data[0] == 0);
      fail_unless (map.data[map.size - 1] == 0);
    } else if (i == 1 && GST_BUFFER_TIMESTAMP (buffer) == 500 * GST_MSECOND) {
      fail_unless (map.data[0] == 0);
      fail_unless (map.data[map.size - 1] == 0);
    } else if (i == 2 && GST_BUFFER_TIMESTAMP (buffer) == 1000 * GST_MSECOND) {
      fail_unless (map.data[0] == 1);
      fail_unless (map.data[map.size - 1] == 1);
    } else if (i == 3 && GST_BUFFER_TIMESTAMP (buffer) == 1500 * GST_MSECOND) {
      fail_unless (map.data[0] == 1);
      fail_unless (map.data[map.size - 1] == 1);
    } else if (i == 4 && GST_BUFFER_TIMESTAMP (buffer) == 2000 * GST_MSECOND) {
      fail_unless (map.data[0] == 2);
      fail_unless (map.data[map.size - 1] == 2);
    } else if (i == 5 && GST_BUFFER_TIMESTAMP (buffer) == 2500 * GST_MSECOND) {
      fail_unless (map.data[0] == 2);
      fail_unless (map.data[map.size - 1] == 2);
    } else if (i == 6 && GST_BUFFER_TIMESTAMP (buffer) == 3000 * GST_MSECOND) {
      fail_unless (map.data[0] == 3);
      fail_unless (map.data[map.size - 1] == 3);
    } else if (i == 7 && GST_BUFFER_TIMESTAMP (buffer) == 3500 * GST_MSECOND) {
      fail_unless (map.data[0] == 3);
      fail_unless (map.data[map.size - 1] == 3);
    } else {
      g_assert_not_reached ();
    }

    gst_buffer_unmap (buffer, &map);

  }
}

GST_START_TEST (test_sync_discont)
{
  run_sync_test (send_buffers_sync_discont, check_buffers_sync_discont);
}

GST_END_TEST;

static Suite *
audiomixer_suite (void)
{
  Suite *s = suite_create ("audiomixer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_caps);
  tcase_add_test (tc_chain, test_filter_caps);
  tcase_add_test (tc_chain, test_event);
  tcase_add_test (tc_chain, test_play_twice);
  tcase_add_test (tc_chain, test_play_twice_then_add_and_play_again);
  tcase_add_test (tc_chain, test_live_seeking);
  tcase_add_test (tc_chain, test_add_pad);
  tcase_add_test (tc_chain, test_remove_pad);
  tcase_add_test (tc_chain, test_clip);
  tcase_add_test (tc_chain, test_duration_is_max);
  tcase_add_test (tc_chain, test_duration_unknown_overrides);
  tcase_add_test (tc_chain, test_loop);
  tcase_add_test (tc_chain, test_flush_start_flush_stop);
  tcase_add_test (tc_chain, test_sync);
  tcase_add_test (tc_chain, test_sync_discont);

  /* Use a longer timeout */
#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    tcase_set_timeout (tc_chain, 5 * 60);
  } else
#endif
  {
    /* this is shorter than the default 60 seconds?! (tpm) */
    /* tcase_set_timeout (tc_chain, 6); */
  }

  return s;
}

GST_CHECK_MAIN (audiomixer);
