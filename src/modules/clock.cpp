#include "modules/clock.hpp"
#include <sstream>
#include <utf8.h>

using zoned_time = date::zoned_time<std::chrono::system_clock::duration>;

struct waybar_time {
  std::locale locale;
  zoned_time ztime;
};

namespace {

size_t utf8_strlen(const std::string& s) {
  return utf8::distance(s.begin(), s.end());
}

std::string utf8_substr(const std::string s, size_t len) {
  utf8::iterator it(s.begin(), s.begin(), s.end());
  for (size_t i = 0; i < len; ++i) {
    ++it;
  }
  int byte_count = it.base() - s.begin();
  return s.substr(0, byte_count);
}

void weekdays_header(const std::locale& locale, const date::weekday& first_dow, std::ostream& os) {
  auto wd = first_dow;
  do {
    if (wd != first_dow) os << ' ';
    auto wd_string = date::format(locale, "%a", wd);
    auto wd_string_len = utf8_strlen(wd_string);
    if (wd_string_len > 2) {
      wd_string = utf8_substr(wd_string, 2);
      wd_string_len = 2;
    }
    const std::string pad(2 - wd_string_len, ' ');
    os << pad << wd_string;
  } while (++wd != first_dow);
  os << "\n";
}

std::string calendar_text(const waybar_time& wtime, const date::weekday& first_dow) {
  const auto daypoint = date::floor<date::days>(wtime.ztime.get_local_time());
  const auto ymd = date::year_month_day(daypoint);
  const date::year_month ym(ymd.year(), ymd.month());
  const auto curr_day = ymd.day();

  std::stringstream os;
  weekdays_header(wtime.locale, first_dow, os);

  // First week prefixed with spaces if needed.
  auto wd = date::weekday(ym/1);
  auto empty_days = (wd - first_dow).count();
  if (empty_days > 0) {
    os << std::string(empty_days * 3 - 1, ' ');
  }
  auto d = date::day(1);
  do {
    if (wd != first_dow) os << ' ';
    if (d == curr_day) {
      os << "<b><u>" << date::format("%e", d) << "</u></b>";
    } else {
      os << date::format("%e", d);
    }
    ++d;
  } while (++wd != first_dow);

  // Following weeks.
  auto last_day = (ym/date::literals::last).day();
  for ( ; d <= last_day; ++d, ++wd) {
    os << ((wd == first_dow) ? '\n' : ' ');
    if (d == curr_day) {
      os << "<b><u>" << date::format("%e", d) << "</u></b>";
    } else {
      os << date::format("%e", d);
    }
  }

  return os.str();
}

}

waybar::modules::Clock::Clock(const std::string& id, const Json::Value& config)
    : ALabel(config, "clock", id, "{:%H:%M}", 60)
    , fixed_time_zone_(false)
{
  if (config_["timezone"].isString()) {
    time_zone_ = date::locate_zone(config_["timezone"].asString());
    fixed_time_zone_ = true;
  }

  if (config_["locale"].isString()) {
    locale_ = std::locale(config_["locale"].asString());
  } else {
    locale_ = std::locale("");
  }

  thread_ = [this] {
    dp.emit();
    auto now = std::chrono::system_clock::now();
    auto timeout = std::chrono::floor<std::chrono::seconds>(now + interval_);
    auto diff = std::chrono::seconds(timeout.time_since_epoch().count() % interval_.count());
    thread_.sleep_until(timeout - diff);
  };
}

auto waybar::modules::Clock::update() -> void {
  if (!fixed_time_zone_) {
    // Time zone can change. Be sure to pick that.
    time_zone_ = date::current_zone();
  }
  auto now = std::chrono::system_clock::now();
  waybar_time wtime = {locale_, date::make_zoned(time_zone_, now)};

  auto text = fmt::format(format_, wtime);
  label_.set_markup(text);

  if (tooltipEnabled()) {
    if (config_["tooltip-format"].isString()) {
      const auto calendar = calendar_text(wtime, date::Sunday);
      auto tooltip_format = config_["tooltip-format"].asString();
      auto tooltip_text = fmt::format(tooltip_format, wtime, fmt::arg("calendar", calendar));
      label_.set_tooltip_markup(tooltip_text);
    } else {
      label_.set_tooltip_markup(text);
    }
  }
}

template <>
struct fmt::formatter<waybar_time> : fmt::formatter<std::tm> {
  template <typename FormatContext>
  auto format(const waybar_time& t, FormatContext& ctx) {
    return format_to(ctx.out(), "{}", date::format(t.locale, fmt::to_string(tm_format), t.ztime));
  }
};
