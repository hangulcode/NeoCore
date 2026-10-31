// 동적 재귀 덤프 대신 원소 타입을 명시한 정수 목록·문자열 키 맵을 출력한다.
public string integers(list<int> values)
{
    string result = "[";
    for (int i in 0, values.len(), 1)
    {
        if (i != 0) result = result .. ", ";
        result = result .. tostring(values[i]);
    }
    return result .. "]";
}

public string counts(map<string, int> values)
{
    string result = "{";
    bool first = true;
    foreach (string key, int value in values)
    {
        if (!first) result = result .. ", ";
        first = false;
        result = result .. "\"" .. key .. "\" = " .. tostring(value);
    }
    return result .. "}";
}
