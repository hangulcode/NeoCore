#include <cassert>
#include "NeoTime.h"
#include "NeoVMInternal.h"
#include "NeoVMWorker.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>

namespace NeoScript
{
namespace TimeLibrary
{
namespace
{
const char spanType = 0;
const char watchType = 0;
const NS_INT intMin = (std::numeric_limits<NS_INT>::min)();
const NS_INT intMax = (std::numeric_limits<NS_INT>::max)();
typedef std::chrono::steady_clock MonotonicClock;

struct Span
{
    const NS_INT milliseconds;
    explicit Span(NS_INT value) : milliseconds(value) {}
};

struct Watch
{
    MonotonicClock::time_point start;
    MonotonicClock::duration accumulated = MonotonicClock::duration::zero();
    bool running = true;
    Watch() : start(MonotonicClock::now()) {}
    MonotonicClock::duration Elapsed() const
    {
        return accumulated + (running ? MonotonicClock::now() - start : MonotonicClock::duration::zero());
    }
};

bool NoNamedMethod(INeoVMWorker*, void*, const VMString*, short);

bool Error(CNeoVMWorker* worker, const char* text)
{
    worker->SetError(text);
    return false;
}

bool Add(NS_INT a, NS_INT b, NS_INT& result)
{
    if ((b > 0 && a > intMax - b) || (b < 0 && a < intMin - b)) return false;
    result = a + b;
    return true;
}

bool Subtract(NS_INT a, NS_INT b, NS_INT& result)
{
    if ((b > 0 && a < intMin + b) || (b < 0 && a > intMax + b)) return false;
    result = a - b;
    return true;
}

template<class T>
bool ReturnObject(CNeoVMWorker* worker, const std::shared_ptr<T>& owner,
    const void* type, Neo_NativeFunction method)
{
    ValueRef result = worker->GetReturnVar();
    worker->ResetVarType(result, VAR_FP_NATIVE, 0);
    return NeoVMSystem::RegisterTableCallBack(result, owner.get(), method, nullptr,
        type, owner.get(), owner);
}

bool ReturnSpan(CNeoVMWorker* worker, NS_INT milliseconds)
{
    return ReturnObject(worker, std::make_shared<Span>(milliseconds), &spanType, NoNamedMethod);
}

const Span* ReadSpan(ValueRef value)
{
    if (value.GetType() == VAR_NONE) return nullptr;
    assert(value.GetType() == VAR_FP_NATIVE && value->_fpNative->_identityType == &spanType);
    return static_cast<const Span*>(value->_fpNative->_pUserData);
}

struct Parts
{
    int sign;
    NS_INT days, hours, minutes, seconds, milliseconds;
    explicit Parts(NS_INT value)
    {
        sign = value < 0 ? -1 : (value > 0 ? 1 : 0);
        // INT64_MIN도 부호 있는 정수에서 직접 부정하지 않고 분해한다.
        uint64_t magnitude = value < 0 ? uint64_t(0) - static_cast<uint64_t>(value)
                                      : static_cast<uint64_t>(value);
        milliseconds = static_cast<NS_INT>(magnitude % 1000); magnitude /= 1000;
        seconds = static_cast<NS_INT>(magnitude % 60); magnitude /= 60;
        minutes = static_cast<NS_INT>(magnitude % 60); magnitude /= 60;
        hours = static_cast<NS_INT>(magnitude % 24);
        days = static_cast<NS_INT>(magnitude / 24);
    }
};

bool ReturnFields(CNeoVMWorker* worker, const char* const* keys, const NS_INT* values, int count)
{
    ValueRef result = worker->GetReturnVar();
    worker->ResetVarType(result, VAR_MAP, count);
    for (int i = 0; i < count; ++i)
    {
        BoundaryValue key;
        worker->Var_SetString(&key, keys[i]);
        // 맵 할당이 예외를 던져도 임시 키의 참조를 반납한다.
        struct KeyGuard
        {
            CNeoVMWorker* worker;
            ValueRef key;
            ~KeyGuard() { worker->Var_Release(key); }
        } guard = {worker, &key};
        ValueRef slot = result->_tbl->Insert(&key);
        if (!slot) return false;
        worker->Var_SetInt(slot, values[i]);
    }
    return true;
}

bool ReturnParts(CNeoVMWorker* worker, NS_INT value)
{
    const Parts parts(value);
    const char* keys[] = {"sign", "days", "hours", "minutes", "seconds", "milliseconds"};
    const NS_INT values[] = {parts.sign, parts.days, parts.hours, parts.minutes, parts.seconds, parts.milliseconds};
    return ReturnFields(worker, keys, values, 6);
}

// 시간 객체의 메서드는 API 표의 번호로 직접 불린다. 이름으로 찾는 경로는 없다.
bool NoNamedMethod(INeoVMWorker*, void*, const VMString*, short) { return false; }

enum class SpanOperation { TotalMilliseconds, TotalSeconds, Parts, Format, Add, Subtract, Compare };

bool SpanCall(CNeoVMWorker* worker, ValueRef receiver, short args, SpanOperation operation)
{
    const Span* span = ReadSpan(receiver);
    if (!span) return Error(worker, "cannot access a null TimeSpan");
    // 반환 슬롯을 교체하면서 마지막 별칭이 해제될 수 있으므로 값을 먼저 복사한다.
    const NS_INT value = span->milliseconds;
    switch (operation)
    {
    case SpanOperation::TotalMilliseconds:
        assert(args == 0);
        worker->ReturnValue(value);
        return true;
    case SpanOperation::TotalSeconds:
        assert(args == 0);
        worker->ReturnValue(static_cast<NS_FLOAT>(static_cast<double>(value) / 1000.0));
        return true;
    case SpanOperation::Parts:
        assert(args == 0);
        return ReturnParts(worker, value);
    case SpanOperation::Format:
    {
        assert(args == 0);
        const Parts parts(value);
        std::ostringstream text;
        text.imbue(std::locale::classic());
        if (parts.sign < 0) text << '-';
        text << parts.days << '.' << std::setfill('0') << std::setw(2) << parts.hours << ':'
             << std::setw(2) << parts.minutes << ':' << std::setw(2) << parts.seconds << '.'
             << std::setw(3) << parts.milliseconds;
        worker->ReturnValue(text.str().c_str());
        return true;
    }
    default: break;
    }
    assert(args == 1);
    const Span* other = ReadSpan(worker->GetStack(1));
    if (!other) return Error(worker, "time_span operation expects a time_span");
    const NS_INT operand = other->milliseconds;
    if (operation == SpanOperation::Compare)
    {
        worker->ReturnValue(value < operand ? -1 : (value > operand ? 1 : 0));
        return true;
    }
    NS_INT result;
    const bool valid = operation == SpanOperation::Add ? Add(value, operand, result) : Subtract(value, operand, result);
    if (!valid) return Error(worker, "time_span milliseconds overflow int64");
    return ReturnSpan(worker, result);
}

enum class WatchOperation { Elapsed, ElapsedMilliseconds, ElapsedMicroseconds, ElapsedSeconds, Start, Stop, Reset, Restart, IsRunning };

bool WatchCall(CNeoVMWorker* worker, ValueRef receiver, short args, WatchOperation operation)
{
    assert(args == 0);
    (void)args;
    if (receiver.GetType() == VAR_NONE) return Error(worker, "cannot access a null Stopwatch");
    assert(receiver.GetType() == VAR_FP_NATIVE && receiver->_fpNative->_identityType == &watchType);
    Watch* watch = static_cast<Watch*>(receiver->_fpNative->_pUserData);
    switch (operation)
    {
    case WatchOperation::Restart:
    case WatchOperation::Reset:
        watch->accumulated = MonotonicClock::duration::zero();
        watch->running = operation == WatchOperation::Restart;
        watch->start = MonotonicClock::now();
        worker->ReturnValue();
        return true;
    case WatchOperation::Start:
        if (!watch->running)
        {
            watch->start = MonotonicClock::now();
            watch->running = true;
        }
        worker->ReturnValue();
        return true;
    case WatchOperation::Stop:
        if (watch->running)
        {
            watch->accumulated = watch->Elapsed();
            watch->running = false;
        }
        worker->ReturnValue();
        return true;
    case WatchOperation::IsRunning:
        worker->ReturnValue(watch->running);
        return true;
    default: break;
    }
    // 밀리초로 먼저 자르지 않고 원래 시계의 정밀도에서 각 단위로 변환한다.
    const MonotonicClock::duration elapsed = watch->Elapsed();
    if (operation == WatchOperation::ElapsedSeconds)
        worker->ReturnValue(std::chrono::duration<NS_FLOAT>(elapsed).count());
    else if (operation == WatchOperation::ElapsedMicroseconds)
        worker->ReturnValue(static_cast<NS_INT>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
    else
    {
        const NS_INT milliseconds = static_cast<NS_INT>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        if (operation == WatchOperation::Elapsed) return ReturnSpan(worker, milliseconds);
        worker->ReturnValue(milliseconds);
    }
    return true;
}

long long DaysBeforeYear(int year)
{
    const long long preceding = year - 1;
    return preceding * 365 + preceding / 4 - preceding / 100 + preceding / 400;
}

bool LeapYear(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

const NS_INT utcMinMs = -2208988800000LL;
const NS_INT utcMaxMs = 253402300799999LL;
const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

bool MakeUtc(const NS_INT* p, NS_INT& result)
{
    if (p[0] < 1900 || p[0] > 9999 || p[1] < 1 || p[1] > 12 || p[2] < 1 ||
        p[3] < 0 || p[3] > 23 || p[4] < 0 || p[4] > 59 || p[5] < 0 || p[5] > 59 ||
        p[6] < 0 || p[6] > 999) return false;
    const int year = static_cast<int>(p[0]), month = static_cast<int>(p[1]);
    if (p[2] > monthDays[month - 1] + (month == 2 && LeapYear(year) ? 1 : 0)) return false;
    NS_INT days = DaysBeforeYear(year) - DaysBeforeYear(1970) + p[2] - 1;
    for (int m = 1; m < month; ++m) days += monthDays[m - 1] + (m == 2 && LeapYear(year) ? 1 : 0);
    result = (((days * 24 + p[3]) * 60 + p[4]) * 60 + p[5]) * 1000 + p[6];
    return true;
}

bool ReadUtc(CNeoVMWorker* worker, short args, std::tm& calendar, int& milliseconds)
{
    assert(args == 1 && worker->GetStack(1).GetType() == VAR_INT);
    const NS_INT value = worker->GetStack(1)->_int;
    if (value < utcMinMs || value > utcMaxMs) return Error(worker, "UTC milliseconds outside years 1900..9999");
    NS_INT seconds = value / 1000;
    milliseconds = static_cast<int>(value % 1000);
    // 음수 Unix 밀리초는 이전 초와 양수 소수부로 분리한다.
    if (milliseconds < 0) { milliseconds += 1000; --seconds; }
    return UtcCalendar(seconds, calendar);
}

int Digits(const std::string& text, size_t offset, size_t count)
{
    int value = 0;
    for (size_t i = offset; i < offset + count; ++i)
    {
        if (text[i] < '0' || text[i] > '9') return -1;
        value = value * 10 + text[i] - '0';
    }
    return value;
}

}

bool TimeMilliseconds(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 0);
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    worker->ReturnValue(static_cast<NS_INT>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
    return true;
}

bool TimeSpanSeconds(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 1 && worker->GetStack(1).IsNumber());
    ValueRef value = worker->GetStack(1);
    if (value.GetType() == VAR_INT)
    {
        if (value->_int < intMin / 1000 || value->_int > intMax / 1000)
            return Error(worker, "time_span milliseconds overflow int64");
        return ReturnSpan(worker, value->_int * 1000);
    }
    // 정확히 절반이면 0에서 먼 쪽으로 반올림한다. 범위 검사 뒤에만 정수로 변환한다.
    const double milliseconds = std::round(value->_float * 1000.0);
    if (!std::isfinite(milliseconds) || milliseconds < static_cast<double>(intMin) ||
        milliseconds >= -static_cast<double>(intMin))
        return Error(worker, "time_span seconds must produce finite int64 milliseconds");
    return ReturnSpan(worker, static_cast<NS_INT>(milliseconds));
}

bool UtcTimeMilliseconds(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 7);
    NS_INT parts[7];
    for (int i = 0; i < 7; ++i)
    {
        ValueRef value = worker->GetStack(i + 1);
        assert(value.GetType() == VAR_INT);
        parts[i] = value->_int;
    }
    NS_INT value;
    if (!MakeUtc(parts, value)) return Error(worker, "invalid UTC calendar components (years 1900..9999)");
    worker->ReturnValue(value);
    return true;
}

bool UtcParts(CNeoVMWorker* worker, ValueRef, short args)
{
    std::tm calendar; int ms;
    if (!ReadUtc(worker, args, calendar, ms)) return false;
    const char* keys[] = {"year", "month", "day", "hour", "minute", "second", "millisecond", "weekday", "day_of_year"};
    const NS_INT values[] = {calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday,
        calendar.tm_hour, calendar.tm_min, calendar.tm_sec, ms, calendar.tm_wday, calendar.tm_yday + 1};
    return ReturnFields(worker, keys, values, 9);
}

bool UtcFormatMilliseconds(CNeoVMWorker* worker, ValueRef, short args)
{
    std::tm calendar; int ms;
    if (!ReadUtc(worker, args, calendar, ms)) return false;
    char text[80];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", calendar.tm_year + 1900,
        calendar.tm_mon + 1, calendar.tm_mday, calendar.tm_hour, calendar.tm_min, calendar.tm_sec, ms);
    worker->ReturnValue(text);
    return true;
}

bool UtcParseMilliseconds(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 1);
    assert(worker->GetStack(1).GetType() == VAR_STRING || worker->GetStack(1).GetType() == VAR_NONE);
    if (worker->GetStack(1).GetType() == VAR_NONE) return false;
    const std::string& text = worker->GetStack(1)->_str->_str;
    // 고정 UTC 형식만 받으며 공백, 오프셋, 윤초, 밀리초보다 긴 소수부는 거부한다.
    const size_t size = text.size();
    if ((size != 20 && (size < 22 || size > 24)) || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[size - 1] != 'Z' ||
        (size != 20 && text[19] != '.')) return Error(worker, "invalid UTC timestamp string");
    NS_INT parts[] = {Digits(text,0,4), Digits(text,5,2), Digits(text,8,2), Digits(text,11,2),
        Digits(text,14,2), Digits(text,17,2), 0};
    if (size != 20)
    {
        const size_t digits = size - 21;
        const int fraction = Digits(text, 20, digits);
        if (fraction < 0) return Error(worker, "invalid UTC timestamp string");
        parts[6] = fraction * (digits == 1 ? 100 : (digits == 2 ? 10 : 1));
    }
    NS_INT value;
    if (!MakeUtc(parts, value)) return Error(worker, "invalid UTC timestamp string");
    worker->ReturnValue(value);
    return true;
}

bool TimeSpanMilliseconds(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 1 && worker->GetStack(1).GetType() == VAR_INT);
    return ReturnSpan(worker, worker->GetStack(1)->_int);
}

bool TimeSpan(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 5);
    const NS_INT multipliers[] = {86400000, 3600000, 60000, 1000, 1};
    NS_INT total = 0;
    for (int i = 0; i < 5; ++i)
    {
        ValueRef value = worker->GetStack(i + 1);
        assert(value.GetType() == VAR_INT);
        const NS_INT scale = multipliers[i];
        if (value->_int < intMin / scale || value->_int > intMax / scale ||
            !Add(total, value->_int * scale, total))
            return Error(worker, "time_span milliseconds overflow int64");
    }
    return ReturnSpan(worker, total);
}

bool Stopwatch(CNeoVMWorker* worker, ValueRef, short args)
{
    assert(args == 0);
    return ReturnObject(worker, std::make_shared<Watch>(), &watchType, NoNamedMethod);
}

#define NEOCORE_TIME_METHOD(NAME, CALL, OPERATION)     bool NAME(CNeoVMWorker* worker, ValueRef receiver, short args) { return CALL(worker, receiver, args, OPERATION); }
NEOCORE_TIME_METHOD(SpanTotalMilliseconds, SpanCall, SpanOperation::TotalMilliseconds)
NEOCORE_TIME_METHOD(SpanTotalSeconds, SpanCall, SpanOperation::TotalSeconds)
NEOCORE_TIME_METHOD(SpanParts, SpanCall, SpanOperation::Parts)
NEOCORE_TIME_METHOD(SpanFormat, SpanCall, SpanOperation::Format)
NEOCORE_TIME_METHOD(SpanAdd, SpanCall, SpanOperation::Add)
NEOCORE_TIME_METHOD(SpanSubtract, SpanCall, SpanOperation::Subtract)
NEOCORE_TIME_METHOD(SpanCompare, SpanCall, SpanOperation::Compare)
NEOCORE_TIME_METHOD(WatchElapsed, WatchCall, WatchOperation::Elapsed)
NEOCORE_TIME_METHOD(WatchElapsedMilliseconds, WatchCall, WatchOperation::ElapsedMilliseconds)
NEOCORE_TIME_METHOD(WatchElapsedMicroseconds, WatchCall, WatchOperation::ElapsedMicroseconds)
NEOCORE_TIME_METHOD(WatchElapsedSeconds, WatchCall, WatchOperation::ElapsedSeconds)
NEOCORE_TIME_METHOD(WatchStart, WatchCall, WatchOperation::Start)
NEOCORE_TIME_METHOD(WatchStop, WatchCall, WatchOperation::Stop)
NEOCORE_TIME_METHOD(WatchReset, WatchCall, WatchOperation::Reset)
NEOCORE_TIME_METHOD(WatchRestart, WatchCall, WatchOperation::Restart)
NEOCORE_TIME_METHOD(WatchIsRunning, WatchCall, WatchOperation::IsRunning)
#undef NEOCORE_TIME_METHOD

bool UtcCalendar(long long seconds, std::tm& calendar)
{
    // CRT의 time_t 폭과 gmtime 음수 제한에 의존하지 않는다. strftime의 공통 연도 범위만 쓴다.
    if (seconds < -2208988800LL || seconds > 253402300799LL) return false;
    long long days = seconds / 86400;
    int daySeconds = static_cast<int>(seconds % 86400);
    if (daySeconds < 0) { daySeconds += 86400; --days; }
    const long long absoluteDays = days + DaysBeforeYear(1970);
    int lower = 1900, upper = 10000;
    while (lower + 1 < upper)
    {
        const int middle = lower + (upper - lower) / 2;
        if (DaysBeforeYear(middle) <= absoluteDays) lower = middle;
        else upper = middle;
    }
    calendar = std::tm();
    calendar.tm_year = lower - 1900;
    calendar.tm_yday = static_cast<int>(absoluteDays - DaysBeforeYear(lower));
    int monthDay = calendar.tm_yday;
    const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while (calendar.tm_mon < 11)
    {
        const int count = monthDays[calendar.tm_mon] + (calendar.tm_mon == 1 && LeapYear(lower) ? 1 : 0);
        if (monthDay < count) break;
        monthDay -= count;
        ++calendar.tm_mon;
    }
    calendar.tm_mday = monthDay + 1;
    calendar.tm_hour = daySeconds / 3600;
    calendar.tm_min = daySeconds / 60 % 60;
    calendar.tm_sec = daySeconds % 60;
    calendar.tm_wday = static_cast<int>((days + 4) % 7);
    if (calendar.tm_wday < 0) calendar.tm_wday += 7;
    return true;
}

}
}
