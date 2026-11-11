#pragma once

#include <ctime>
#include <vector>

namespace NeoScript
{
class CNeoVMWorker;
struct BoundaryValue;
class ValueRef;

namespace TimeLibrary
{
bool TimeMilliseconds(CNeoVMWorker* worker, ValueRef, short args);
bool TimeSpan(CNeoVMWorker* worker, ValueRef, short args);
bool TimeSpanMilliseconds(CNeoVMWorker* worker, ValueRef, short args);
bool TimeSpanSeconds(CNeoVMWorker* worker, ValueRef, short args);
bool UtcTimeMilliseconds(CNeoVMWorker* worker, ValueRef, short args);
bool UtcParts(CNeoVMWorker* worker, ValueRef, short args);
bool UtcFormatMilliseconds(CNeoVMWorker* worker, ValueRef, short args);
bool UtcParseMilliseconds(CNeoVMWorker* worker, ValueRef, short args);
bool Stopwatch(CNeoVMWorker* worker, ValueRef, short args);
// 시간 객체의 메서드. 수신자는 TimeSpan/Stopwatch 값이다.
bool SpanTotalMilliseconds(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanTotalSeconds(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanParts(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanFormat(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanAdd(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanSubtract(CNeoVMWorker* worker, ValueRef receiver, short args);
bool SpanCompare(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchElapsed(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchElapsedMilliseconds(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchElapsedMicroseconds(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchElapsedSeconds(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchStart(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchStop(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchReset(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchRestart(CNeoVMWorker* worker, ValueRef receiver, short args);
bool WatchIsRunning(CNeoVMWorker* worker, ValueRef receiver, short args);
bool UtcCalendar(long long seconds, std::tm& calendar);
}
}
