// 공개 함수·별칭·전이 의존성과 타입별 덤프 모듈을 함께 실행한다.
import mod1;
import mod2 as arithmetic;
import dump;

list<int> values = [mod1.add(1, 2), arithmetic.sub(10, 4), arithmetic.mul(3, 4)];
string formatted = dump.integers(values);
map<string, int> counts = {};
counts["items"] = values.len();
string mapped = dump.counts(counts);

bool passed = values[0] == 3 && values[1] == 6 && values[2] == 12;
passed = passed && formatted == "[3, 6, 12]" && mapped == "{\"items\" = 3}";
print(formatted);
print(mapped);
if (passed) print("module checks ok");
else print("FAIL|module");

export bool CheckPassed()
{
    return passed;
}
