#pragma once

#include <string>
class MprisService;
class OsdOverlay;

struct MprisOsdData {
  std::string Title;
  std::string Artist;

  bool operator==(const MprisOsdData& d) const { return d.Artist == Artist && d.Title == Title; }
};

class MprisOsd {
public:
  void bindOverlay(OsdOverlay& overlay);
  void onMprisChanged(const MprisService& service);

private:
  OsdOverlay* m_overlay = nullptr;
  MprisOsdData lastData;
};
