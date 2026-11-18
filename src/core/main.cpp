// license:GPLv3+

// Implementation of WinMain (Windows with UI) or main (Standalone)

#include "core/stdafx.h"

#include "vpversion.h"

#include "plugins/VPXPlugin.h"
#include "core/VPXPluginAPIImpl.h"

#include "core/AppCommands.h"
#include "core/VPApp.h"

#ifndef __STANDALONE__
#include "ui/win/Win32DialogSink.h"
#endif

#include <initguid.h>

#define SET_CRT_DEBUG_FIELD(a) _CrtSetDbgFlag((a) | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG))

#include <locale>
#include <codecvt>

#ifdef __STANDALONE__
#include <filesystem>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE const char* VPinballGetVersionStringFull()
{
   return VP_VERSION_STRING_FULL_LITERAL;
}
#endif

#if defined(__STANDALONE__) && ((defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32) || defined(__EMSCRIPTEN__) || (defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX))
#include <csignal>
#ifndef _MSC_VER
#include <unistd.h>
#endif

void OnSignalHandler(int signum)
{
   // A signal handler runs on whichever thread receives the signal, while the game loop, the emulation and
   // the render threads keep running. exit() from here would run the atexit handlers and the static
   // destructors of the executable and of every loaded plugin library under those running threads. So the
   // handler only asks the player to close: the game loop then unloads the plugins and stops the emulation,
   // and the process leaves through the normal exit.
   // A signal repeated after a few seconds means that shutdown is stuck. Then the process ends with _exit(),
   // which returns to the kernel at once and runs none of the above, the only way out that is safe here.
   static volatile time_t closeRequestedAt = 0;
   const time_t now = time(nullptr);
   if (g_pplayer != nullptr && (closeRequestedAt == 0 || now - closeRequestedAt < 3))
   {
      if (closeRequestedAt == 0)
      {
         closeRequestedAt = now;
         PLOGI.printf("Closing from signal: %d", signum);
      }
      g_pplayer->SetCloseState(Player::CloseState::CS_CLOSE_APP);
      return;
   }
   PLOGI.printf("Exiting from signal: %d", signum);
   _exit(-9999);
}
#endif

#if defined(__STANDALONE__) && defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX
bool OnAppTerminate()
{
   if (g_pplayer == nullptr)
      return false;
   PLOGI << "Closing from application quit";
   g_pplayer->SetCloseState(Player::CloseState::CS_CLOSE_APP);
   return true;
}
#endif


#if defined(ENABLE_OPENGL) && !defined(__STANDALONE__)
// The OpenGL implementation will fail on NVIDIA drivers when Threaded Optimization is enabled so we disable it for this app
// Note: There are quite a lot of applications doing this, but I think this may hide an incorrect OpenGL call somewhere
// that the threaded optimization of NVIDIA drivers ends up to crash. This would be good to find the root cause, if any.

#include "nvapi/nvapi.h"
#include "nvapi/NvApiDriverSettings.h"
#pragma warning(push)
#pragma warning(disable : 4838)
#include "nvapi/NvApiDriverSettings.c"
#pragma warning(pop)

enum NvThreadOptimization
{
   NV_THREAD_OPTIMIZATION_AUTO = 0,
   NV_THREAD_OPTIMIZATION_ENABLE = 1,
   NV_THREAD_OPTIMIZATION_DISABLE = 2,
   NV_THREAD_OPTIMIZATION_NO_SUPPORT = 3
};

static bool NvAPI_OK_Verify(NvAPI_Status status)
{
   if (status == NVAPI_OK)
      return true;

   NvAPI_ShortString szDesc = {};
   NvAPI_GetErrorMessage(status, szDesc);

   PLOGI << "NVAPI error: " << szDesc;

   return false;
}

static NvThreadOptimization GetNVIDIAThreadOptimization()
{
   NvThreadOptimization threadOptimization = NV_THREAD_OPTIMIZATION_NO_SUPPORT;

   NvAPI_Status status;
   status = NvAPI_Initialize();
   if (!NvAPI_OK_Verify(status))
      return threadOptimization;

   NvDRSSessionHandle hSession;
   status = NvAPI_DRS_CreateSession(&hSession);
   if (!NvAPI_OK_Verify(status))
      return threadOptimization;

   status = NvAPI_DRS_LoadSettings(hSession);
   if (!NvAPI_OK_Verify(status))
   {
      NvAPI_DRS_DestroySession(hSession);
      return threadOptimization;
   }

   NvDRSProfileHandle hProfile;
   status = NvAPI_DRS_GetBaseProfile(hSession, &hProfile);
   if (!NvAPI_OK_Verify(status))
   {
      NvAPI_DRS_DestroySession(hSession);
      return threadOptimization;
   }

   NVDRS_SETTING originalSetting = {};
   originalSetting.version = NVDRS_SETTING_VER;
   status = NvAPI_DRS_GetSetting(hSession, hProfile, OGL_THREAD_CONTROL_ID, &originalSetting);
   if (NvAPI_OK_Verify(status))
   {
      threadOptimization = (NvThreadOptimization)originalSetting.u32CurrentValue;
   }

   NvAPI_DRS_DestroySession(hSession);

   return threadOptimization;
}

static void SetNVIDIAThreadOptimization(NvThreadOptimization threadedOptimization)
{
   if (threadedOptimization == NV_THREAD_OPTIMIZATION_NO_SUPPORT)
      return;

   NvAPI_Status status;
   status = NvAPI_Initialize();
   if (!NvAPI_OK_Verify(status))
      return;

   NvDRSSessionHandle hSession;
   status = NvAPI_DRS_CreateSession(&hSession);
   if (!NvAPI_OK_Verify(status))
      return;

   status = NvAPI_DRS_LoadSettings(hSession);
   if (!NvAPI_OK_Verify(status))
   {
      NvAPI_DRS_DestroySession(hSession);
      return;
   }

   NvDRSProfileHandle hProfile;
   status = NvAPI_DRS_GetBaseProfile(hSession, &hProfile);
   if (!NvAPI_OK_Verify(status))
   {
      NvAPI_DRS_DestroySession(hSession);
      return;
   }

   NVDRS_SETTING setting = {};
   setting.version = NVDRS_SETTING_VER;
   setting.settingId = OGL_THREAD_CONTROL_ID;
   setting.settingType = NVDRS_DWORD_TYPE;
   setting.u32CurrentValue = (EValues_OGL_THREAD_CONTROL)threadedOptimization;

   status = NvAPI_DRS_SetSetting(hSession, hProfile, &setting);
   if (!NvAPI_OK_Verify(status))
   {
      NvAPI_DRS_DestroySession(hSession);
      return;
   }

   status = NvAPI_DRS_SaveSettings(hSession);
   NvAPI_OK_Verify(status);

   NvAPI_DRS_DestroySession(hSession);
}
#endif

extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
   #if defined(ENABLE_OPENGL) && !defined(__STANDALONE__)
   static NvThreadOptimization s_OriginalNVidiaThreadOptimization = NV_THREAD_OPTIMIZATION_NO_SUPPORT;
   #endif

   Logger::Init();

#ifndef __STANDALONE__
   // Default message sink for the whole application lifetime: report user messages through
   // plain Win32 message boxes. UI contexts (Win32 editor, player) install their own sinks.
   Win32DialogSink win32DialogSink;
   ScopedUserMessageSink scopedMessageSink(&win32DialogSink);
#endif

   const auto run = []() -> int
   {
      #if defined(ENABLE_OPENGL) && !defined(__STANDALONE__)
      s_OriginalNVidiaThreadOptimization = GetNVIDIAThreadOptimization();
      if (s_OriginalNVidiaThreadOptimization != NV_THREAD_OPTIMIZATION_NO_SUPPORT && s_OriginalNVidiaThreadOptimization != NV_THREAD_OPTIMIZATION_DISABLE)
      {
         PLOGI << "Disabling NVIDIA Threaded Optimization";
         SetNVIDIAThreadOptimization(NV_THREAD_OPTIMIZATION_DISABLE);
      }
      #endif

      VPApp theApp;
      CommandLineProcessor cmdLine;
      cmdLine.ProcessCommandLine();
      theApp.InitInstance(dynamic_cast<PlayTableCommand*>(cmdLine.m_command.get()) != nullptr);

      // The video subsystem is initialized lazily when a window is created (see VPX::Window), so
      // headless commands (info, script/POV export, audit, tournament validation) run without a
      // display or a working video driver.

      // Run the application
      if (!cmdLine.m_command)
         return 0;
      cmdLine.m_command->Execute();
      return cmdLine.m_command->GetExitCode();
   };

   int retval = 0;
#ifdef __STANDALONE__
   try
   {
      retval = run();
   }
   catch (const std::exception &e)
   {
      ShowMessage(MsgSeverity::Fatal, e.what(), "Unhandled exception");
      retval = -1;
   }
#else
   // Unexpected exceptions are not caught, so that they can reach the crash handler (stack trace, minidump).
   // win32xx exceptions (of the editor) are reported by Win32EditCommand
   retval = run();
#endif

   #if defined(ENABLE_OPENGL) && !defined(__STANDALONE__) 
   if (s_OriginalNVidiaThreadOptimization != NV_THREAD_OPTIMIZATION_NO_SUPPORT && s_OriginalNVidiaThreadOptimization != NV_THREAD_OPTIMIZATION_DISABLE)
   {
      PLOGI << "Restoring NVIDIA Threaded Optimization";
      SetNVIDIAThreadOptimization(s_OriginalNVidiaThreadOptimization);
   }
   #endif

   SDL_Quit();

   PLOGI << "Closing VPX...\n\n";
   return retval;
}

#if defined(__STANDALONE__) && ((defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32) || defined(__EMSCRIPTEN__))
extern int g_argc;
extern const char **g_argv;

#ifdef _WIN32
// Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
// Game Vault (Xbox): start-up as an Xbox app. Game Vault starts a table with the link
// vpinball:?table=<path>[&launchOnExit=gamevault:]; started from the tile, the first table in LocalState/VPinballX/tables
// plays, else the test table that ships in the package.
#include <appmodel.h>
#define SDL_MAIN_NOIMPL
#include <SDL3/SDL_main.h>
#undef main // SDL_main.h renames main; this file keeps its own

static bool VpxIsPackagedApp()
{
   UINT32 len = 0;
   return GetCurrentPackageFullName(&len, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}

static string VpxUrlDecode(const string& in)
{
   string out;
   for (size_t i = 0; i < in.size(); i++)
   {
      if (in[i] == '%' && i + 2 < in.size() && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2]))
      {
         out += (char)strtol(in.substr(i + 1, 2).c_str(), nullptr, 16);
         i += 2;
      }
      else
         out += in[i] == '+' ? ' ' : in[i];
   }
   return out;
}

static void VpxXboxLog(const string& base, const string& line)
{
   if (FILE* f = fopen((base + "xbox_start.txt").c_str(), "a"))
   {
      fprintf(f, "%s\n", line.c_str());
      fclose(f);
   }
}

// Messages for the player (errors before the log opens too) go to xbox_start.txt
class VpxXboxMessageSink final : public UserMessageSink
{
public:
   string m_base;
   void Notify(MsgSeverity severity, const string& title, const string& message) override { VpxXboxLog(m_base, "message: " + title + ": " + message); }
};

static int SDLCALL VpxXboxMain(int, char**)
{
   // SDL audio open hangs in an Xbox app: SDL gets its silent driver and AudioPlayer plays through XAudio2
   SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
   SDL_SetHint("VPX_XBOX_AUDIO", "1");
   SDL_SetHint("VPX_XBOX_APP", "1");
   // Windows.Gaming.Input: the only controller API in an Xbox app (our SDL keeps it off unless asked)
   SDL_SetHint("SDL_JOYSTICK_WGI", "1");
   // The activation (and its link) arrives while SDL pumps the window's events
   SDL_InitSubSystem(SDL_INIT_VIDEO);
   const char* uri = nullptr;
   for (int i = 0; i < 300 && (uri = SDL_GetHint("SDL_WINRT_ACTIVATION_URI")) == nullptr; i++)
   {
      SDL_PumpEvents();
      SDL_Delay(10);
   }
   string link = uri ? uri : "";

   char* pref = SDL_GetPrefPath("", "VPinballX");
   const string base = pref ? pref : "";
   SDL_free(pref);
   const char* install = SDL_GetBasePath();
   const string installDir = install ? install : "";
   if (FILE* f = fopen((base + "xbox_start.txt").c_str(), "w"))
      fclose(f);
   VpxXboxLog(base, "VPX for Game Vault, link: " + link);

   string table;
   const size_t q = link.find("table=");
   if (q != string::npos)
   {
      const size_t end = link.find('&', q);
      table = VpxUrlDecode(link.substr(q + 6, end == string::npos ? string::npos : end - q - 6));
   }
   if (table.empty())
   {
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(base + "tables", ec))
         if (e.is_regular_file() && lowerCase(e.path().extension().string()) == ".vpx")
         {
            table = e.path().string();
            break;
         }
   }
   if (table.empty())
      table = installDir + "tables\\test000-default-table.vpx";
   VpxXboxLog(base, "table: " + table);

   static string s_base, s_table;
   s_base = base;
   s_table = table;
   static const char* s_argv[] = { "VPinballX", "-PrefPath", s_base.c_str(), "-Play", s_table.c_str(), nullptr };
   g_argc = 5;
   g_argv = s_argv;
   static VpxXboxMessageSink s_sink;
   s_sink.m_base = base;
   SetUserMessageSink(&s_sink);
   const int ret = WinMain(NULL, NULL, NULL, 0);
   VpxXboxLog(base, "exit code " + std::to_string(ret));
   return ret;
}
#endif

int main(int argc, const char** argv) {
#ifdef _WIN32
   signal(SIGINT, OnSignalHandler);
#else
   struct sigaction sigIntHandler;
   sigIntHandler.sa_handler = OnSignalHandler;
   sigemptyset(&sigIntHandler.sa_mask);
   sigIntHandler.sa_flags = 0;
   sigaction(SIGINT, &sigIntHandler, nullptr);
#endif

#ifdef _WIN32
   // Game Vault (Xbox): inside an app package this is an Xbox app. SDL (our sdl3-uwp) then runs the WinRT app and
   // calls VpxXboxMain from the app thread.
   if (VpxIsPackagedApp())
      return SDL_RunApp(0, nullptr, VpxXboxMain, nullptr);
#endif

   g_argc = argc;
   g_argv = argv;
   return WinMain(NULL, NULL, NULL, 0);
}
#endif
