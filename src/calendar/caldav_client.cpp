#include "calendar/caldav_client.h"

#include "calendar/ical_parser.h"
#include "core/log.h"
#include "net/http_client.h"
#include "pugixml.hpp"
#include "time/time_format.h"
#include "util/base64.h"

#include <string_view>

namespace calendar {

  namespace {
    constexpr Logger kLog("calendar.caldav");

    std::string
    buildReportBody(std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end) {
      const std::string s = formatUtcTime(start, "%Y%m%dT%H%M%SZ");
      const std::string e = formatUtcTime(end, "%Y%m%dT%H%M%SZ");
      return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
             "<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
             "<D:prop><C:calendar-data><C:expand start=\""
          + s
          + "\" end=\""
          + e
          + "\"/></C:calendar-data></D:prop>"
            "<C:filter><C:comp-filter name=\"VCALENDAR\"><C:comp-filter name=\"VEVENT\">"
            "<C:time-range start=\""
          + s
          + "\" end=\""
          + e
          + "\"/></C:comp-filter></C:comp-filter></C:filter>"
            "</C:calendar-query>";
    }

    std::string_view localName(const char* qualified) {
      std::string_view name(qualified);
      const std::size_t colon = name.rfind(':');
      return colon == std::string_view::npos ? name : name.substr(colon + 1);
    }

    // Collect the text of every element whose local name is "calendar-data".
    void collectCalendarData(const pugi::xml_node& node, std::vector<std::string>& out) {
      for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_element) {
          if (localName(child.name()) == "calendar-data") {
            const char* text = child.text().get();
            if (text != nullptr && text[0] != '\0') {
              out.emplace_back(text);
            }
          }
          collectCalendarData(child, out);
        }
      }
    }
  } // namespace

  void fetchCalDavEvents(
      HttpClient& http, const CalDavAccount& account, std::chrono::system_clock::time_point start,
      std::chrono::system_clock::time_point end, std::function<void(bool, std::vector<CalendarEvent>)> cb
  ) {
    HttpRequest req;
    req.method = "REPORT";
    req.url = account.url;
    req.body = buildReportBody(start, end);
    req.headers = {
        "Authorization: Basic " + Base64::encode(account.username + ":" + account.password),
        "Depth: 1",
        "Content-Type: application/xml; charset=utf-8",
    };

    const std::string calendarName = account.calendarName;
    const std::string color = account.color;
    http.request(std::move(req), [cb = std::move(cb), calendarName, color](HttpResponse resp) {
      if (!resp.transportOk || (resp.status != 207 && resp.status != 200)) {
        kLog.warn("caldav REPORT failed http={}", resp.status);
        cb(false, {});
        return;
      }

      pugi::xml_document doc;
      const pugi::xml_parse_result parsed = doc.load_buffer(resp.body.data(), resp.body.size());
      if (!parsed) {
        kLog.warn("caldav response XML parse error: {}", parsed.description());
        cb(false, {});
        return;
      }

      std::vector<std::string> calendarDataBlocks;
      collectCalendarData(doc, calendarDataBlocks);

      std::vector<CalendarEvent> events;
      for (const std::string& ics : calendarDataBlocks) {
        for (CalendarEvent& event : parseICalEvents(ics)) {
          event.calendarName = calendarName;
          event.colorHex = color;
          events.push_back(std::move(event));
        }
      }
      cb(true, std::move(events));
    });
  }

} // namespace calendar
