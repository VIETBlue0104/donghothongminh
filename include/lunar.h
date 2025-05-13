#ifndef LUNAR_CONVERTER_H
#define LUNAR_CONVERTER_H

#include <math.h>

// Cấu trúc lưu thông tin lịch âm cho một năm
struct LunarYearInfo {
  int year;        // Năm dương lịch
  int leapMonth;   // Tháng nhuận (0 nếu không có)
  int offsetOfTet; // Độ lệch Tết âm so với ngày 1/1 dương
  uint32_t lunarMonthLengths; // 12 hoặc 13 tháng âm, mỗi bit là 29 hay 30 ngày
};

// Dữ liệu mẫu cho một số năm (từ 2020 đến 2030) — bạn có thể mở rộng thêm
const LunarYearInfo lunarData[] = {
  {2020, 4, 25, 0x955555},  // năm nhuận: tháng 4 nhuận
  {2021, 0, 12, 0x4ae4bb},
  {2022, 0, 1,  0xa4d2ba},
  {2023, 0, 22, 0xd0d4ae},
  {2024, 5, 10, 0xd55555},  // năm nhuận: tháng 5 nhuận
  {2025, 0, 29, 0x56a5ad},
  {2026, 0, 17, 0x9ad4da},
  {2027, 6, 6,  0x4ae4ae},  // năm nhuận: tháng 6 nhuận
  {2028, 0, 26, 0xa4d6d4},
  {2029, 0, 13, 0xd4a95b},
  {2030, 0, 3,  0xb6aab5}
};
const int lunarDataSize = sizeof(lunarData) / sizeof(LunarYearInfo);

// Hàm nội bộ: lấy JDN từ ngày dương
int getJDN(int dd, int mm, int yy) {
  int a = (14 - mm) / 12;
  int y = yy + 4800 - a;
  int m = mm + 12 * a - 3;
  return dd + (153 * m + 2) / 5 + 365 * y + y/4 - y/100 + y/400 - 32045;
}

// Hàm nội bộ: chuyển JDN về ngày tháng năm
void jdnToDate(int jdn, int &dd, int &mm, int &yy) {
  int a = jdn + 32044;
  int b = (4 * a + 3) / 146097;
  int c = a - (b * 146097) / 4;
  int d = (4 * c + 3) / 1461;
  int e = c - (1461 * d) / 4;
  int m = (5 * e + 2) / 153;

  dd = e - (153 * m + 2) / 5 + 1;
  mm = m + 3 - 12 * (m / 10);
  yy = b * 100 + d - 4800 + (m / 10);
}

// Trả về số ngày trong tháng âm lịch thứ 'monthIndex' (0-based)
int getLunarMonthLength(const LunarYearInfo &info, int monthIndex) {
  if (monthIndex >= 13) return 0;
  return (info.lunarMonthLengths >> (12 - monthIndex)) & 1 ? 30 : 29;
}

// Hàm chính: Chuyển đổi dương lịch sang âm lịch
void convertSolar2Lunar(int dd, int mm, int yy, int &lunarDay, int &lunarMonth, int &lunarYear, bool &isLeap) {
  int jdn = getJDN(dd, mm, yy);

  // Tìm năm âm lịch gần nhất
  const LunarYearInfo* info = nullptr;
  for (int i = 0; i < lunarDataSize; ++i) {
    if (lunarData[i].year == yy) {
      info = &lunarData[i];
      break;
    } else if (lunarData[i].year > yy) {
      info = &lunarData[i - 1];
      break;
    }
  }

  // Nếu không tìm thấy thông tin năm
  if (!info) {
    lunarDay = dd;
    lunarMonth = mm;
    lunarYear = yy;
    isLeap = false;
    return;
  }

  // JDN của Tết âm lịch
  int jdnTet = getJDN(1, 1, info->year) + info->offsetOfTet;

  // Nếu ngày dương trước Tết âm, dùng năm trước
  if (jdn < jdnTet) {
    info = nullptr;
    for (int i = 0; i < lunarDataSize - 1; ++i) {
      if (lunarData[i].year == yy - 1) {
        info = &lunarData[i];
        jdnTet = getJDN(1, 1, info->year) + info->offsetOfTet;
        break;
      }
    }
    if (!info) {
      lunarDay = dd;
      lunarMonth = mm;
      lunarYear = yy;
      isLeap = false;
      return;
    }
  }

  int offset = jdn - jdnTet;
  int month = 1;
  isLeap = false;
  int monthIndex = 0;

  while (monthIndex < (info->leapMonth ? 13 : 12)) {
    int daysInMonth = getLunarMonthLength(*info, monthIndex);
    if (offset < daysInMonth) break;
    offset -= daysInMonth;

    if (info->leapMonth != 0 && month == info->leapMonth) {
      if (!isLeap) {
        isLeap = true;
      } else {
        isLeap = false;
        month++;
      }
    } else {
      month++;
    }
    monthIndex++;
  }

  lunarDay = offset + 1;
  lunarMonth = month;
  lunarYear = info->year;
}

#endif  // LUNAR_CONVERTER_H
