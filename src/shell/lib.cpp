#ifdef WIN32
#include <windows.h>
#elif __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/syslimits.h>
#include <stdlib.h>
#else
#include <dlfcn.h>
#include <linux/limits.h>
#include <cstring>
#endif

#include <vector>
#include <string>
#include <codecvt>
#include <locale>
#include <map>

#include <audience.h>

#include "../common/trace.h"
#include "../common/safefn.h"
#include "webserver/process.h"
#include "nucleus.h"
#include "util.h"

nucleus_init_t nucleus_init = nullptr;
nucleus_window_create_t nucleus_window_create = nullptr;
nucleus_window_destroy_t nucleus_window_destroy = nullptr;
nucleus_loop_t nucleus_loop = nullptr;

std::map<void *, std::shared_ptr<WebserverHandle>> _audience_webserver_registry;

bool audience_is_initialized()
{
  return nucleus_init != nullptr && nucleus_window_create != nullptr && nucleus_window_destroy != nullptr && nucleus_loop != nullptr;
}

static bool _audience_init()
{
  if (audience_is_initialized())
  {
    return true;
  }

  std::vector<std::string> dylibs{
#ifdef WIN32
      "audience_windows_edge.dll",
      "audience_windows_ie11.dll",
#elif __APPLE__
      "libaudience_macos_webkit.dylib",
#else
      "libaudience_unix_webkit.so",
#endif
  };

  for (auto dylib : dylibs)
  {
    auto dylib_abs = dir_of_exe() + PATH_SEPARATOR + dylib;
    TRACEA(info, "trying to load library from path " << dylib_abs);
#ifdef WIN32
    auto dlh = LoadLibraryA(dylib_abs.c_str());
#else
    auto dlh = dlopen(dylib_abs.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    if (dlh != nullptr)
    {
#ifdef WIN32
#define LookupFunction GetProcAddress
#else
#define LookupFunction dlsym
#endif

      nucleus_init = (nucleus_init_t)LookupFunction(dlh, "audience_init");
      nucleus_window_create = (nucleus_window_create_t)LookupFunction(dlh, "audience_window_create");
      nucleus_window_destroy = (nucleus_window_destroy_t)LookupFunction(dlh, "audience_window_destroy");
      nucleus_loop = (nucleus_loop_t)LookupFunction(dlh, "audience_loop");

      if (!audience_is_initialized())
      {
        TRACEA(info, "could not find function pointer in library " << dylib);
      }

      if (audience_is_initialized() && nucleus_init())
      {
        TRACEA(info, "library " << dylib << " loaded successfully");
        return true;
      }
      else
      {
        TRACEA(info, "could not initialize library " << dylib);
      }

      nucleus_init = nullptr;
      nucleus_window_create = nullptr;
      nucleus_window_destroy = nullptr;
      nucleus_loop = nullptr;

#ifdef WIN32
      FreeLibrary(dlh);
#else
      dlclose(dlh);
#endif
    }
    else
    {
      TRACEA(info, "could not load library " << dylib);
#ifdef WIN32
      TRACEW(info, GetLastErrorString().c_str());
#endif
    }
  }

  return false;
}

bool audience_init()
{
  return SAFE_FN(_audience_init, false)();
}

void *_audience_window_create(const AudienceWindowDetails *details)
{
  if (!audience_is_initialized())
  {
    return nullptr;
  }

  // create url based application window
  if (details->webapp_type == AUDIENCE_WEBAPP_TYPE_URL)
  {
    return nucleus_window_create(details);
  }

  // create directory based application window
  if (details->webapp_type == AUDIENCE_WEBAPP_TYPE_DIRECTORY)
  {
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

    // start webserver on available port
    std::string address = "127.0.0.1";
    unsigned short ws_port = 0;

    auto ws_handle = webserver_start(address, ws_port, converter.to_bytes(details->webapp_location), 3);

    // construct url of webapp
    auto webapp_url = std::wstring(L"http://") + converter.from_bytes(address) + L":" + std::to_wstring(ws_port) + L"/";

    // create window
    AudienceWindowDetails new_details{
        AUDIENCE_WEBAPP_TYPE_URL,
        webapp_url.c_str(),
        details->loading_title};

    auto window_handle = nucleus_window_create(&new_details);

    if (window_handle == nullptr)
    {
      webserver_stop(ws_handle);
      return nullptr;
    }

    // attach webserver to registry
    _audience_webserver_registry[window_handle] = ws_handle;

    // return window handle
    return window_handle;
  }

  throw std::invalid_argument("invalid webapp type");
}

void *audience_window_create(const AudienceWindowDetails *details)
{
  return SAFE_FN(_audience_window_create, nullptr)(details);
}

void _audience_window_destroy(void *handle)
{
  if (!audience_is_initialized())
  {
    return;
  }

  // destroy window
  nucleus_window_destroy(handle);

  // check if we have to stop a running webservice
  auto i = _audience_webserver_registry.find(handle);
  if (i != _audience_webserver_registry.end())
  {
    webserver_stop(i->second);
    _audience_webserver_registry.erase(i);
  }
}

void audience_window_destroy(void *handle)
{
  SAFE_FN(_audience_window_destroy)
  (handle);
}

void _audience_loop()
{
  if (!audience_is_initialized())
  {
    return;
  }
  nucleus_loop();
}

void audience_loop()
{
  SAFE_FN(_audience_loop)
  ();
}
