import system;

void PrintParts(map<string, int> parts)
{
    foreach (string key, int value in parts) print(key .. ": " .. tostring(value));
}

print("Unix seconds: " .. tostring(system.time()));
print("Unix milliseconds: " .. tostring(system.time_ms()));
print("UTC: " .. system.utc_date("%Y-%m-%dT%H:%M:%SZ", system.time()));
print("Before epoch: " .. system.utc_date("%Y-%m-%d %H:%M:%S %Z", -1));
int eventMs = system.utc_time_ms(2026, 10, 26, 12, 30, 0, 125);
string eventText = system.utc_format_ms(eventMs);
print("UTC milliseconds: " .. eventText);
print("UTC round trip: " .. tostring(system.utc_parse_ms(eventText) == eventMs));
PrintParts(system.utc_parts(eventMs));
print("Before epoch millisecond: " .. system.utc_format_ms(-1));

TimeSpan duration = system.time_span(1, 2, 3, 4, 5);
TimeSpan longer = duration.add(system.time_span_ms(995));
print("Duration: " .. duration.format());
print("Added: " .. longer.format());
print("Difference: " .. tostring(longer.subtract(duration).total_milliseconds()));
print("Compare: " .. tostring(duration.compare(longer)));
PrintParts(system.time_span_ms(-90250).parts());
print("Fractional seconds: " .. system.time_span_seconds(1.2345).format());
if (longer.subtract(duration).total_milliseconds() != 995) { int zero = 0; print(1 / zero); }
if (system.utc_parse_ms(eventText) != eventMs) { int zero = 0; print(1 / zero); }

Stopwatch watch = system.stopwatch();
int total = 0;
for (int i in 0, 1000) total += i;
print("Sum: " .. tostring(total));
print("Elapsed milliseconds: " .. tostring(watch.elapsed_ms()));
print("Elapsed duration: " .. watch.elapsed().format());
watch.stop();
int stopped = watch.elapsed_us();
print("Stopped microseconds: " .. tostring(stopped));
print("Stopped seconds: " .. tostring(watch.elapsed_seconds()));
if (watch.elapsed_us() != stopped) { int zero = 0; print(1 / zero); }
watch.start();
watch.reset();
print("Running after reset: " .. tostring(watch.is_running()));
if (watch.is_running() || watch.elapsed_ms() != 0) { int zero = 0; print(1 / zero); }
watch.restart();
print("Restarted: " .. tostring(watch.is_running()));
