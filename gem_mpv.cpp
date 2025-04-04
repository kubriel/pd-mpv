#include "gem_mpv.hpp"
#include <string.h>

// greatly inspired by mvp-examples/libmpv/sdl

CPPEXTERN_NEW_WITH_GIMME(mpv);

static void wakeup(void *ctx)
{
  static_cast<mpv*>(ctx)->rise_event_flag();
}

#ifdef __linux__
static void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
  // TODO adapt for Windows and MacOS
  return (void *)glXGetProcAddress((const GLubyte*)name);
}
#endif

#ifdef __APPLE__
#import <mach-o/dyld.h>
#import <stdlib.h>
#import <string.h>
static void * get_proc_address_mpv (void *fn_ctx, const char *name)
{
    NSSymbol symbol;
    char *symbolName;
    symbolName = (char*)malloc (strlen (name) + 2); // 1
    strcpy(symbolName + 1, name); // 2
    symbolName[0] = '_'; // 3
    symbol = NULL;
    if (NSIsSymbolNameDefined (symbolName)) // 4
        symbol = NSLookupAndBindSymbol (symbolName);
    free (symbolName); // 5
    return symbol ? NSAddressOfSymbol (symbol) : NULL; // 6
}
#endif

static void node_to_atom(const mpv_node* node, std::vector<t_atom>& res)
{
  switch(node->format)
  {
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING:
    {
      t_atom a;
      SETSYMBOL(&a, gensym(node->u.string));
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_FLAG:
    {
      t_atom a;
      SETFLOAT(&a, node->u.flag);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_INT64:
    {
      t_atom a;
      SETFLOAT(&a, node->u.int64);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_DOUBLE:
    {
      t_atom a;
      SETFLOAT(&a, node->u.double_);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_NODE_ARRAY:
    {
      t_atom a;
      for(int i = 0; i< node->u.list->num; i++)
      {
        auto val = node->u.list->values[i];
        node_to_atom(&val, res);
      }
      break;
    }
    case MPV_FORMAT_BYTE_ARRAY:
    case MPV_FORMAT_NODE_MAP:
    {
      pd_error(0, "could not handle this node format : %d", node->format);
      break;
    }
    case MPV_FORMAT_NODE:
    case MPV_FORMAT_NONE:
      break;
  }
}

static void prop_to_atom(mpv_event_property* prop, std::vector<t_atom>& res)
{
  const char* name = prop->name;
  if(name)
  {
    t_atom aname;
    SETSYMBOL(&aname, gensym(name));
    res.push_back(std::move(aname));
  }

  if(!prop->data)
    return;

  switch(prop->format){
    case MPV_FORMAT_FLAG:
    {
      t_atom a;
      bool b = *(bool *)prop->data;
      SETFLOAT(&a, b ? 1. : 0.);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_DOUBLE:
    {
      t_atom a;
      double d = *(double *)prop->data;
      SETFLOAT(&a, d);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_INT64:
    {
      t_atom a;
      int64_t i = *(int64_t*)prop->data;
      SETFLOAT(&a, i);
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_STRING:
    case MPV_FORMAT_OSD_STRING:
    {
      t_atom a;
      char* s = (char *)prop->data;
      SETSYMBOL(&a, gensym(s));
      res.push_back(std::move(a));
      break;
    }
    case MPV_FORMAT_NODE:
    {
      mpv_node* node = (mpv_node*)prop->data;
      node_to_atom(node, res);
      break;
    }
    case MPV_FORMAT_NONE:
      break;
    case MPV_FORMAT_NODE_ARRAY:
    case MPV_FORMAT_NODE_MAP:
    case MPV_FORMAT_BYTE_ARRAY:
      std::cout << "format: " << prop->format << " not handled" << std::endl;
  }
}

static void post_mpv_log(mpv* x, mpv_event_log_message* msg)
{
  int level;
  switch(msg->log_level)
  {
    case MPV_LOG_LEVEL_FATAL:
      level=0;
      break;
    case MPV_LOG_LEVEL_ERROR:
      level=1;
      break;
    case MPV_LOG_LEVEL_WARN:
    case MPV_LOG_LEVEL_INFO:
      level=2;
      break;
    case MPV_LOG_LEVEL_V:
    case MPV_LOG_LEVEL_DEBUG:
      level=3;
      break;
    default:
      level=41;
  }
  if(level != -1)
    logpost(x->x_obj, level, "[%s] %s: %s", msg->prefix, msg->level, msg->text);
}

mpv::mpv(int argc, t_atom*argv)
  : gemframebuffer(argc, argv)
{
  m_prop_outlet = outlet_new(this->x_obj, 0);

  m_mpv = mpv_create();
  if (!m_mpv)
  {
    error("mpv: context init failed");
    return;
  }

  // Some minor options can only be set before mpv_initialize().
  if (mpv_initialize(m_mpv) < 0)
    error("mpv: init failed");

  // Actually using the opengl_cb state has to be explicitly requested.
  // Otherwise, mpv will create a separate platform window.
  if (mpv_set_option_string(m_mpv, "vo", "libmpv") < 0)
  //if (mpv_set_option_string(m_mpv, "vo", "vdpau") < 0)
  {
    error("failed to set VO");
    return;
  }

// i hope to use it with vaapi hardware decoding at some point
/*if (mpv_set_option_string(m_mpv, "hwdec", "vaapi") < 0)
  {
    error("failed to set hwdec");
    return;
  }*/

// sometimes i use it with jack for audio playback
/*if (mpv_set_option_string(m_mpv, "ao", "jack") < 0)
  {
    error("failed to set hwdec");
    return;
  }*/


  mpv_request_event(m_mpv, MPV_EVENT_TICK, 1);
  mpv_set_wakeup_callback(m_mpv, wakeup, this);
}

mpv::~mpv()
{
  if(m_mpv)
    mpv_terminate_destroy(m_mpv);
}

void mpv::render(GemState *state)
{
  m_event_flag=true; // FIXME it appears that the flag is not always rised by wakeup fn
  while(m_mpv && m_event_flag && !m_modified)
  {
    mpv_event *event = mpv_wait_event(m_mpv, 0);
    switch (event->event_id)
    {
      case MPV_EVENT_NONE:
        m_event_flag=false;
        break;
      case MPV_EVENT_SHUTDOWN:
        break;
      case MPV_EVENT_LOG_MESSAGE:
      {
        struct mpv_event_log_message *msg = (struct mpv_event_log_message *)event->data;
        post_mpv_log(this, msg);
        break;
      }
      case MPV_EVENT_GET_PROPERTY_REPLY:
      {
        if(event->error != MPV_ERROR_SUCCESS)
        {
          error("error while getting property");
        } else {
          auto prop = (mpv_event_property*) event->data;
          handle_prop_event(prop);
        }
        break;
      }
      case MPV_EVENT_SET_PROPERTY_REPLY:
      case MPV_EVENT_COMMAND_REPLY:
        break;
      case MPV_EVENT_START_FILE:
      {
        m_started=false;
        t_atom a;
        SETSYMBOL(&a, gensym("start"));
        outlet_anything(m_prop_outlet, gensym("event"), 1, &a);
        break;
      }
      case MPV_EVENT_END_FILE:
      {
        m_started=false;
        t_atom a;
        SETSYMBOL(&a, gensym("end"));
        outlet_anything(m_prop_outlet, gensym("event"), 1, &a);
        break;
      }
      case MPV_EVENT_FILE_LOADED:
      {
        t_atom a;
        SETSYMBOL(&a, gensym("file_loaded"));
        outlet_anything(m_prop_outlet, gensym("event"), 1, &a);

        // observed properties doesn't always send changes
        // or at least we may have missed it
        mpv_get_property_async(m_mpv, 'w', "width",  MPV_FORMAT_INT64);
        mpv_get_property_async(m_mpv, 'h', "height", MPV_FORMAT_INT64);
        mpv_get_property_async(m_mpv, 'd', "duration", MPV_FORMAT_DOUBLE);
        break;
      }
/*DEPRECATED
#ifdef MPV_ENABLE_DEPRECATED
      case MPV_EVENT_TRACKS_CHANGED:
      case MPV_EVENT_TRACK_SWITCHED:
        break;
#endif
*/
      case MPV_EVENT_IDLE:
        break;
/*DEPRECATED
#ifdef MPV_ENABLE_DEPRECATED
      case MPV_EVENT_PAUSE:
      case MPV_EVENT_UNPAUSE:
        break;
#endif
*/
      case MPV_EVENT_TICK:
      {
        m_new_frame=true;
        if(m_started)
        {
          {
            static t_atom argv[2];
            SETSYMBOL(argv, gensym("d"));
            SETSYMBOL(argv+1, gensym("time-pos"));
            command_mess(gensym("property_typed"), 2, argv);
          }
          {
            static t_atom argv[2];
            SETSYMBOL(argv, gensym("d"));
            SETSYMBOL(argv+1, gensym("percent-pos"));
            command_mess(gensym("property_typed"), 2, argv);
          }
        }

        /*
         * This might be interesting but it's a lot of output
        t_atom a;
        SETSYMBOL(&a, gensym("new_frame"));
        outlet_anything(m_prop_outlet, gensym("event"), 1, &a);
        */
        break;
      }
/*DEPRECATED
#ifdef MPV_ENABLE_DEPRECATED
      case MPV_EVENT_SCRIPT_INPUT_DISPATCH:
        break;
#endif
*/
      case MPV_EVENT_CLIENT_MESSAGE:
      case MPV_EVENT_VIDEO_RECONFIG:
      case MPV_EVENT_AUDIO_RECONFIG:
        break;
/*DEPRECATED
#ifdef MPV_ENABLE_DEPRECATED
      case MPV_EVENT_METADATA_UPDATE:
        break;
#endif
*/
      case MPV_EVENT_SEEK:
      case MPV_EVENT_PLAYBACK_RESTART:
        m_started = true;
        t_atom a;
        SETSYMBOL(&a, gensym("playback_restart"));
        outlet_anything(m_prop_outlet, gensym("event"), 1, &a);
        break;
      case MPV_EVENT_PROPERTY_CHANGE:
      {
        mpv_event_property *prop = (mpv_event_property *)event->data;
        handle_prop_event(prop);
        break;
      }
/*DEPRECATED
#ifdef MPV_ENABLE_DEPRECATED
      case MPV_EVENT_CHAPTER_CHANGE:
        break;
#endif
*/
      case MPV_EVENT_QUEUE_OVERFLOW:
      // case MPV_EVENT_HOOK: // not yet available in v0.27.2
        break;
    }
  }

  if(m_auto_resize && (m_media_width != m_width || m_media_height != m_height) )
  {
    gemframebuffer::dimMess(m_media_width, m_media_height);
    m_reload = true;
  }

  if(m_reload && !m_modified)
  {
    m_reload=false;
    // reload file when size changed
    if(m_loadfile_cmd.size() > 1)
      command_mess(gensym("command"), m_loadfile_cmd.size(), m_loadfile_cmd.data());
  }

  if(m_new_frame && !m_modified)
  {
    gemframebuffer::render(state);
    if(m_mpv_gl)
    {
      mpv_opengl_fbo mpfbo{static_cast<int>(m_frameBufferIndex), m_width, m_height, 0};
      mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
      int flip_y{1};
      mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        //{MPV_RENDER_PARAM_X11_DISPLAY, dpy},
        {MPV_RENDER_PARAM_INVALID, nullptr}
      };
      mpv_render_context_render(m_mpv_gl, params);
    }
  }
}

void mpv::postrender(GemState *state)
{
  if(m_new_frame)
  {
    gemframebuffer::postrender(state);
    m_new_frame = false;
  }
}
void mpv::startRendering(void)
{
  gemframebuffer::startRendering();

  if(!m_mpv)
    return;

  // not necessary with render API
  // The OpenGL API is somewhat separate from the normal mpv API. This only
  // returns NULL if no OpenGL support is compiled.
  //m_mpv_gl = static_cast<mpv_render_context*>(mpv_get_sub_api(m_mpv, MPV_SUB_API_OPENGL_CB));
  //mpv_render_context *m_mpv_gl;
  /*if (!m_mpv_gl)
  {
    error("failed to create mpv GL API handle");
    return;
  }*/

  // This makes mpv use the currently set GL context. It will use the callback
  // to resolve GL builtin functions, as well as extensions.
  mpv_opengl_fbo mpfbo{static_cast<int>(m_frameBufferIndex), m_width, m_height, 0};
  mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
  int flip_y{1};

  mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
//        {MPV_RENDER_PARAM_X11_DISPLAY, dpy},
        {MPV_RENDER_PARAM_INVALID, nullptr}
      };
  //if (mpv_render_context_create(m_mpv_gl, NULL, get_proc_address_mpv, NULL) < 0)
  if (mpv_render_context_create(&m_mpv_gl, m_mpv, params) < 0)
  {
    error("failed to initialize mpv GL context");
    return;
  }

  m_reload = true;
}

void mpv::stopRendering(void)
{
  gemframebuffer::stopRendering();

  if(m_mpv_gl)
    mpv_render_context_free(m_mpv_gl);
}

void mpv::command_mess(t_symbol *s, int argc, t_atom *argv)
{
  std::string types{};

  if(s == gensym("command_typed") || s == gensym("property_typed"))
  {
    if(argc < 2 || argv->a_type != A_SYMBOL || (argv+1)->a_type != A_SYMBOL)
    {
      error("usage : command_typed|property_typed <types> <command> <arguments...>");
      return;
    }
    types = std::string(argv->a_w.w_symbol->s_name);
    argv++;
    argc--;
  }

  else if(argc == 0 || argv->a_type != A_SYMBOL )
  {
    error("usage: command|property <command> <arguments...>");
    return;
  }

  if(!m_mpv)
  {
    error("mvp not initialized");
    return;
  }


  if(s == gensym("command") || s == gensym("command_typed"))
  {
    if(!types.empty())
      types = " " + types;
    if(argc>1 && argv->a_type == A_SYMBOL)
    {
      const char* s = argv->a_w.w_symbol->s_name;
      if( 0 == strcmp("loadfile", s) )
      {
        m_started=false;

        // save loadfile command call to reload file if needed
        m_loadfile_cmd.clear();
        m_loadfile_cmd.reserve(argc);
        for(int i=0; i<argc; ++i)
          m_loadfile_cmd.push_back(argv[i]);
      }
    }
    node_builder node(argc, argv, types);
    if(mpv_command_node_async(m_mpv, 0, node.node()) < 0)
    {
      error("Error when executing command");
      return;
    }
  }
  else if (s == gensym("property") || s == gensym("property_typed"))
  {
    auto sname = argv->a_w.w_symbol;
    auto name = argv->a_w.w_symbol->s_name;
    argv++;
    argc--;

    if(argc > 0)
    {
      node_builder node(argc, argv, types);
      auto err = mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_NODE, std::move(node.node()));
      if(err != MPV_ERROR_SUCCESS)
      {
        error("can't set property %s: error code: %d", name, err);
      }
    }
    else
    {
      mpv_format format = MPV_FORMAT_DOUBLE;
      if(!types.empty())
      {
        switch (types.at(0)) {
          case 'b':
            format = MPV_FORMAT_FLAG;
            break;
          case 'i':
            format = MPV_FORMAT_INT64;
            break;
          case 's':
            format = MPV_FORMAT_STRING;
            break;
          case 'd':
          default:
            format = MPV_FORMAT_DOUBLE;
            break;
        }
      }
      auto err = mpv_get_property_async(m_mpv, 0, name, format);
      if(err != MPV_ERROR_SUCCESS)
      {
        error("can't send get property %s request, error code: %d", name, err);
        return;
      }
    }
  }
}

void mpv::handle_prop_event(mpv_event_property *prop)
{
  if (strcmp(prop->name, "width") == 0) {
    if (prop->format == MPV_FORMAT_INT64) {
      int64_t val = *(int64_t *)prop->data;
      m_media_width = val;
    }
  } else if (strcmp(prop->name, "height") == 0) {
    if (prop->format == MPV_FORMAT_INT64) {
      int64_t val = *(int64_t *)prop->data;
      m_media_height = val;
    }
  }

  std::vector<t_atom> a;
  a.reserve(256);
  prop_to_atom(prop, a);
  outlet_anything(m_prop_outlet, gensym("property"), a.size(), a.data());
}

void mpv :: dimen_mess(int width, int height)
{
  if(width < 0 && height < 0)
  {
    m_auto_resize = true;
    gemframebuffer::dimMess(m_media_width, m_media_height);
  }
  else
  {
    m_auto_resize = false;
    gemframebuffer::dimMess(width, height);
  }
}

void mpv::log_mess(std::string level)
{
  if(!m_mpv)
    return;

  auto err = mpv_request_log_messages(m_mpv, level.c_str());
  if(err != MPV_ERROR_SUCCESS)
  {
    error("can't set log level to '%s', error code: %d", level.c_str(), err);
  }
}

void mpv::obj_setupCallback(t_class *classPtr)
{
  gemframebuffer::obj_setupCallback(classPtr);

  CPPEXTERN_MSG2(classPtr, "dimen",  dimen_mess, int, int); // override gemframebuffer method
  CPPEXTERN_MSG(classPtr, "command",  command_mess);
  CPPEXTERN_MSG(classPtr, "command_typed",  command_mess);
  CPPEXTERN_MSG(classPtr, "property",  command_mess);
  CPPEXTERN_MSG(classPtr, "property_typed",  command_mess);
  CPPEXTERN_MSG1(classPtr, "log_level",  log_mess, std::string);
}
