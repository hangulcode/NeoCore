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
bool UtcCalendar(long long seconds, std::tm& calendar);
}
}
