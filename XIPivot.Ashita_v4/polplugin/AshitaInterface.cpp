/*
 * 	Copyright � 2019-2020, Renee Koecher
* 	All rights reserved.
 * 
 * 	Redistribution and use in source and binary forms, with or without
 * 	modification, are permitted provided that the following conditions are met :
 * 
 * 	* Redistributions of source code must retain the above copyright
 * 	  notice, this list of conditions and the following disclaimer.
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in the
 * 	  documentation and/or other materials provided with the distribution.
 * 	* Neither the name of XIPivot nor the
 * 	  names of its contributors may be used to endorse or promote products
 * 	  derived from this software without specific prior written permission.
 * 
 * 	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * 	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * 	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * 	DISCLAIMED.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * 	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * 	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * 	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * 	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * 	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * 	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "AshitaInterface.h"
#include "MemCache.h"

#include <regex>

namespace XiPivot
{
	namespace Pol
	{

		AshitaInterface::AshitaInterface(const char* args)
			: Core::Redirector(), IPolPlugin(),
			m_pluginArgs(args ? args : "")
		{
			/* FIXME: does this play anywhere nice with reloads?
			 * FIXME: .. I hope it does
			 */
			Redirector::s_instance = this;
		}

		const char* AshitaInterface::GetName(void) const        { return u8"XIPivot"; }
		const char* AshitaInterface::GetAuthor(void) const      { return u8"Heals"; }
		const char* AshitaInterface::GetDescription(void) const { return u8"Runtime DAT / music replacement manager"; }
		const char* AshitaInterface::GetLink(void) const        { return u8"https://github.com/shirk/XIPivot"; }
		double AshitaInterface::GetVersion(void) const          { return 4.01; }

		bool AshitaInterface::Initialize(IAshitaCore* core, ILogManager* log, uint32_t id)
		{
			bool initialized = true;

			IPolPlugin::Initialize(core, log, id);
			instance().setLogProvider(this);

			auto config = m_AshitaCore->GetConfigurationManager();
			if (config != nullptr)
			{
				if (m_settings.load(config))
				{
					instance().setDebugLog(m_settings.debugLog);
					instance().setRootPath(m_settings.rootPath);
					for (const auto& path : m_settings.overlays)
					{
						instance().addOverlay(path);
					}
				}

				if (m_settings.cacheEnabled)
				{
					Core::MemCache::instance().setLogProvider(this);
					Core::MemCache::instance().setDebugLog(m_settings.debugLog);
					Core::MemCache::instance().setCacheAllocation(m_settings.cacheSize);
				}
				m_settings.save(config);
			}

			if (m_settings.cacheEnabled)
			{
				initialized &= Core::MemCache::instance().setupHooks();
			}
			initialized &= instance().setupHooks();
			return initialized;
		}

		void AshitaInterface::Release(void)
		{
			if (m_settings.cacheEnabled)
			{
				Core::MemCache::instance().releaseHooks();
			}
			instance().releaseHooks();
			IPolPlugin::Release();
		}

		/* ILogProvider */
		void AshitaInterface::logMessage(Core::ILogProvider::LogLevel level, std::string msg)
		{
			logMessageF(level, msg);
		}

		void AshitaInterface::logMessageF(Core::ILogProvider::LogLevel level, std::string msg, ...)
		{
			if (level != Core::ILogProvider::LogLevel::Discard)
			{
				char msgBuf[512];
				Ashita::LogLevel ashitaLevel = Ashita::LogLevel::None;

				switch (level)
				{
				case Core::ILogProvider::LogLevel::Discard: /* never acutally reached */
					return;

				case Core::ILogProvider::LogLevel::Debug:
					ashitaLevel = Ashita::LogLevel::Debug;
					break;

				case Core::ILogProvider::LogLevel::Info:
					ashitaLevel = Ashita::LogLevel::Info;
					break;

				case Core::ILogProvider::LogLevel::Warn:
					ashitaLevel = Ashita::LogLevel::Warn;
					break;

				case Core::ILogProvider::LogLevel::Error:
					ashitaLevel = Ashita::LogLevel::Error;
					break;
				}

				va_list args;
				va_start(args, msg);

				vsnprintf_s(msgBuf, 511, msg.c_str(), args);
				m_LogManager->Log(static_cast<uint32_t>(ashitaLevel), GetName(), msgBuf);

				va_end(args);
			}
		}
		/* private parts below */

		AshitaInterface::Settings::Settings()
		{
			char workPath[MAX_PATH];

			GetCurrentDirectoryA(MAX_PATH, static_cast<LPSTR>(workPath));

			/* default to "plugin location"/DATs */
			rootPath = std::string(workPath) + "/DATs";
			overlays.clear();
			debugLog = false;
			cacheEnabled = false;
			cacheSize = 0;
			cachePurgeDelay = 600;
		}

		bool AshitaInterface::Settings::load(IConfigurationManager* config)
		{
			if (config->Load("XIPivot", "XIPivot"))
			{
				const char* rP = config->GetString("XIPivot", "settings", "root_path");
				const bool dbg = config->GetBool("XIPivot", "settings", "debug_log", true);

				debugLog = dbg;
				rootPath = (rP ? rP : rootPath);

				overlays.clear();

				unsigned overlayIndex = 0;
				char overlayIndexStr[10];
				char* overlayName = nullptr;

				do
				{
					snprintf(overlayIndexStr, sizeof(overlayIndexStr) - 1, "%d", overlayIndex++);
					overlayName = const_cast<char*>(config->GetString("XIPivot", "overlays", overlayIndexStr));

					if (overlayName != nullptr && strcmp(overlayName, "") != 0)
					{
						overlays.push_back(overlayName);
					}
				} while (overlayName != nullptr);

				cacheEnabled = config->GetBool("XIPivot", "cache", "enabled", false);
				cacheSize = config->GetInt32("XIPivot", "cache", "size", 2048) * 0x100000;
				cachePurgeDelay = config->GetInt32("XIPivot", "cache", "max_age", 600);

				return true;
			}
			return false;
		}

		void AshitaInterface::Settings::save(IConfigurationManager* config)
		{
			//config->Remove("XIPivot");
			//config->Save("XIPivot", "XIPivot");

			config->SetValue("XIPivot", "settings", "root_path", rootPath.c_str());
			config->SetValue("XIPivot", "settings", "debug_log", debugLog ? "true" : "false");

			for (unsigned i = 0; ; ++i)
			{
				char key[10];
				snprintf(key, 9, "%d", i);

				if (i < overlays.size())
				{
					config->SetValue("XIPivot", "overlays", key, overlays.at(i).c_str());
				}
				else
				{
					if (config->GetString("XIPivot", "overlays", key) != nullptr)
					{
						config->SetValue("XIPivot", "overlays", key, "");
					}
					else
					{
						break;
					}
				}
			}

			config->SetValue("XIPivot", "cache", "enabled", cacheEnabled ? "true" : "false");

			char val[32];
			snprintf(val, 31, "%u", cacheSize / 0x100000);
			config->SetValue("XIPivot", "cache", "size", val);


			snprintf(val, 31, "%u", cachePurgeDelay);
			config->SetValue("XIPivot", "cache", "max_age", val);


			config->Save("XIPivot", "XIPivot");
		}
	}
}