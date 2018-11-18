// Copyright 2018 Zhang Li <StarsunYzL@gmail.com>.
// 
// This program is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.If not, see < https://www.gnu.org/licenses/>.

#include <cinttypes>
#include <cstdio>
#include <memory>
#include <string>
#include <regex>
#include <windows.h>
#include "_plugins.h"
#include "_scriptapi_module.h"

const int kPluginVersion = 0x000001;

enum MenuEntry {
  kMenuLoadMapFile,
  kMenuAbout
};

#define _U8(PRI) u8##PRI
#define U8(PRI) _U8(PRI)

#ifndef _WIN64
using useg = uint16_t;
using uoff = uint32_t;
#else
using useg = uint32_t;
using uoff = uint64_t;
#endif

struct MapInfo {
  useg segment;
  uoff offset;
  std::string name;
};

HWND g_dlg_handle = nullptr;
int g_menu_handle = 0;

bool ApplyName(useg segment, uoff offset, const std::string& name, void* userdata) {
  BridgeList<Script::Module::ModuleSectionInfo>* section_list = reinterpret_cast<BridgeList<Script::Module::ModuleSectionInfo>*>(userdata);
  if (static_cast<int>(segment) > section_list->Count() || offset > (*section_list)[segment - 1].size)
    return false;

  return DbgSetAutoLabelAt((*section_list)[segment - 1].addr + offset, name.c_str());
}

using ParseCallback = decltype(&ApplyName);

unsigned int ParseMapFile(const wchar_t* path, ParseCallback callback, void* userdata) {
  unsigned int applied = 0;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path, L"r")) {
    _plugin_logprintf(u8"[MapLdr] Could not open map file.\n");
    return applied;
  }

  std::shared_ptr<FILE> file_sp(file, [](FILE* p) {
    if (p)
      fclose(p);
  });

  std::regex regex_header(R"(^\s*Address\s+Publics\s+by\s+(?:Value|Name)\s*$)");
  std::regex regex_name(R"(^\s*([0-9a-fA-F]{4,8}):([0-9a-fA-F]{8,16})\s+([[:print:]]+?)\s*$)");
  std::cmatch match;

  bool found = false;
  std::shared_ptr<char[]> buf(new char[1024]);
  while (fgets(buf.get(), 1024, file_sp.get())) {
    if (!found) {
      if (std::regex_match(buf.get(), regex_header))
        found = true;
    } else {
      if (std::regex_match(buf.get(), match, regex_name)) {
        if (callback(static_cast<useg>(strtoul(match[1].str().c_str(), nullptr, 16)),
#ifndef _WIN64
          static_cast<uoff>(strtoul(match[2].str().c_str(), nullptr, 16)),
#else
          static_cast<uoff>(strtoull(match[2].str().c_str(), nullptr, 16)),
#endif
          match[3].str(), userdata))
          ++applied;
      }
    }
  }
  return applied;
}

void LoadMapFile() {
  if (!DbgIsDebugging()) {
    _plugin_logprintf(u8"[MapLdr] The debugger is not running.\n");
    return;
  }

  wchar_t path[MAX_PATH] = {L'\0'};
  OPENFILENAME ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_dlg_handle;
  ofn.lpstrFilter = L"Map Files\0*.map\0All Files\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path) / sizeof(path[0]);
  ofn.Flags = OFN_FILEMUSTEXIST;
  if (!GetOpenFileName(&ofn))
    return;

  SELECTIONDATA selection;
  if (!GuiSelectionGet(GUI_DISASSEMBLY, &selection)) {
    _plugin_logprintf(u8"[MapLdr] Could not get current module.\n");
    return;
  }

  char name[MAX_MODULE_SIZE] = {u8'\0'};
  DbgFunctions()->ModNameFromAddr(selection.start, name, true);
  duint base = DbgFunctions()->ModBaseFromAddr(selection.start);
#ifndef _WIN64
  _plugin_logprintf(u8"[MapLdr] %08" U8(PRIXPTR) u8" %s\n", base, name);
#else
  _plugin_logprintf(u8"[MapLdr] %016" U8(PRIXPTR) u8" %s\n", base, name);
#endif

  BridgeList<Script::Module::ModuleSectionInfo> section_list;
  Script::Module::SectionListFromAddr(base, &section_list);
  if (!section_list.Count()) {
    _plugin_logprintf(u8"[MapLdr] Could not get sections.\n");
    return;
  }

  for (int i = 0; i != section_list.Count(); ++i) {
#ifndef _WIN64
    _plugin_logprintf(u8"[MapLdr]   %08" U8(PRIXPTR) u8" %08" U8(PRIXPTR) u8" %s\n", section_list[i].addr, section_list[i].size, section_list[i].name);
#else
    _plugin_logprintf(u8"[MapLdr]   %016" U8(PRIXPTR) u8" %016" U8(PRIXPTR) u8" %s\n", section_list[i].addr, section_list[i].size, section_list[i].name);
#endif
  }

  unsigned int applied = ParseMapFile(path, ApplyName, std::addressof(section_list));
  _plugin_logprintf(u8"[MapLdr] Applied %u names.\n", applied);
}

void About() {
  MessageBox(g_dlg_handle, LR"(MapLdr v0.0.1

Zhang Li <StarsunYzL@gmail.com>
2018-11-14, Shenzhen)", L"About", MB_OK | MB_ICONINFORMATION);
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* init) {
  init->sdkVersion = PLUG_SDKVERSION;
  init->pluginVersion = kPluginVersion;
  strcpy_s(init->pluginName, u8"MapLdr");
  return true;
}

extern "C" __declspec(dllexport) bool plugstop() {
  _plugin_menuclear(g_menu_handle);
  return true;
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT* setup) {
  g_dlg_handle = setup->hwndDlg;
  g_menu_handle = setup->hMenu;
  _plugin_menuaddentry(setup->hMenu, kMenuLoadMapFile, u8"Load Map File...");
  _plugin_menuaddentry(setup->hMenu, kMenuAbout, u8"About");
}

extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE type, PLUG_CB_MENUENTRY* info) {
  switch (info->hEntry) {
    case kMenuLoadMapFile:
      LoadMapFile();
      break;
    case kMenuAbout:
      About();
      break;
  }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
  return TRUE;
}
