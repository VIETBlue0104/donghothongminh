#ifndef LUNAR_CONVERTER_H
#define LUNAR_CONVERTER_H

#include <cmath>

#define JD_EPOCH 2415021.076998695
#define PI M_PI
#define TIMEZONE_OFFSET (0.5 + 7.0 / 24.0)  // Múi giờ GMT+7

// Đổi ngày dương lịch sang số ngày Julius (JDN)
int64_t jdFromDate(int dd, int mm, int yy) {
    int a = (14 - mm) / 12;
    int y = yy + 4800 - a;
    int m = mm + 12 * a - 3;
    int64_t jdn = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    if (jdn < 2299161) {
        jdn = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
    }
    return jdn;
}

// Tính thời điểm sóc (New Moon) lần thứ k
int64_t NewMoon(int k) {
    double T = k / 1236.85;
    double T2 = T * T;
    double T3 = T2 * T;
    double T4 = T3 * T;
    double dr = PI / 180.0;

    double jde = 2415020.75933 + 29.53058868 * k
               + 0.0001178 * T2 - 0.000000155 * T3
               + 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);

    double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
    double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
    double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;

    double C = (0.1734 - 0.000393 * T) * sin(M * dr)
             + 0.0021 * sin(2 * M * dr)
             - 0.4068 * sin(Mpr * dr)
             + 0.0161 * sin(2 * Mpr * dr)
             - 0.0004 * sin(3 * Mpr * dr)
             + 0.0104 * sin(2 * F * dr)
             - 0.0051 * sin((M + Mpr) * dr)
             - 0.0074 * sin((M - Mpr) * dr)
             + 0.0004 * sin((2 * F + M) * dr)
             - 0.0004 * sin((2 * F - M) * dr)
             - 0.0006 * sin((2 * F + Mpr) * dr)
             + 0.0010 * sin((2 * F - Mpr) * dr)
             + 0.0005 * sin((M + 2 * Mpr) * dr);

    double deltaT = (T < -11) ? 
        0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T4 :
        -0.000278 + 0.000265 * T + 0.000262 * T2;
    
    return static_cast<int64_t>(jde + C - deltaT + TIMEZONE_OFFSET);
}

// Tính kinh độ mặt trời tại JDN
double SunLongitude(double jdn) {
    double T = (jdn - 2451545.0) / 36525.0;
    double T2 = T * T;
    double T3 = T2 * T;
    double dr = PI / 180.0;

    double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T3;
    double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
    double C = (1.914600 - 0.004817 * T - 0.000014 * T2) * sin(dr * M)
             + (0.019993 - 0.000101 * T) * sin(dr * 2 * M)
             + 0.000290 * sin(dr * 3 * M);

    double L = L0 + C;
    return fmod(L, 360.0);
}

// Tìm ngày sóc gần cuối tháng 11 âm lịch của năm dương lịch
int64_t getLunarMonth11(int yy) {
    double off = jdFromDate(31, 12, yy) - JD_EPOCH;
    int k = static_cast<int>(off / 29.530588853);
    int64_t nm = NewMoon(k);
    if (SunLongitude(nm) >= 9.0) {
        nm = NewMoon(k - 1);
    }
    return nm;
}

// Tính tháng nhuận (nếu có)
int getLeapMonthOffset(int64_t a11) {
    int k = static_cast<int>((a11 - JD_EPOCH) / 29.530588853 + 0.5);
    int lastArc = static_cast<int>(SunLongitude(NewMoon(k + 1)) / 30);
    int i = 2;
    int arc = static_cast<int>(SunLongitude(NewMoon(k + i)) / 30);
    while (arc == lastArc && i < 14) {
        lastArc = arc;
        i++;
        arc = static_cast<int>(SunLongitude(NewMoon(k + i)) / 30);
    }
    return i - 1;
}

// Hàm chuyển đổi dương lịch sang âm lịch
void convertSolar2Lunar(int dd, int mm, int yy, int &lunarDay, int &lunarMonth, int &lunarYear, bool &isLeap) {
    int64_t dayNumber = jdFromDate(dd, mm, yy);
    int k = static_cast<int>((dayNumber - JD_EPOCH) / 29.530588853 + 0.5);
    int64_t monthStart = NewMoon(k + 1);
    if (monthStart > dayNumber) {
        monthStart = NewMoon(k);
    }

    int64_t a11 = getLunarMonth11(yy);
    int64_t b11 = (a11 < monthStart) ? getLunarMonth11(yy + 1) : getLunarMonth11(yy - 1);

    lunarYear = (a11 >= monthStart) ? yy : yy + 1;

    lunarDay = static_cast<int>(dayNumber - monthStart + 1);
    int diff = static_cast<int>((monthStart - a11) / 29);
    isLeap = false;
    lunarMonth = diff + 11;

    if (b11 - a11 > 365) {
        int leapMonth = getLeapMonthOffset(a11);
        if (diff >= leapMonth) {
            lunarMonth--;
            if (diff == leapMonth) {
                isLeap = true;
            }
        }
    }

    if (lunarMonth > 12) lunarMonth -= 12;
    if (lunarMonth >= 11 && diff < 4) lunarYear--;
}

#endif // LUNAR_CONVERTER_H
