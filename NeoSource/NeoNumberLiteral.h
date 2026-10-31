#pragma once

#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <sstream>

namespace NeoScript
{

// 정적 프런트엔드와 기존 로더가 같은 문법·반올림·로케일 규칙으로 10진 리터럴을 읽는다.
inline bool ParseDecimalLiteral(double& r, const char* p)
{
    const char* original = p;
    bool negative = false;
    if (*p == '-' || *p == '+')
    {
        negative = (*p == '-');
        ++p;
    }
    // 10진수 형식 처리

    // 정수부 파싱
    double intPart = 0.0;
    bool hasDigits = false;
    while (std::isdigit(*p)) {
        hasDigits = true;
        intPart = intPart * 10 + (*p - '0');
        p++;
    }

    // 소수부 파싱 (있다면)
    double fracPart = 0.0;
    if (*p == '.') {
        p++;
        bool fracDigits = false;
        double factor = 0.1;
        while (std::isdigit(*p)) {
            fracDigits = true;
            fracPart += (*p - '0') * factor;
            factor /= 10;
            p++;
        }
        // 정수부, 소수부 모두 숫자가 없으면 오류 처리
        if (!hasDigits && !fracDigits)
            return false;
    }
    else if (!hasDigits) {
        // 정수부가 하나도 없고 소수점이 없는 경우
        return false;
    }

    double value = intPart + fracPart;

    // 선택적 지수부 파싱 (e 또는 E 사용)
    if (*p == 'e' || *p == 'E') {
        p++;
        bool expNegative = false;
        if (*p == '-' || *p == '+') {
            expNegative = (*p == '-');
            p++;
        }
        if (!std::isdigit(*p))
            return false;  // 지수부는 최소 한 자리 숫자 필요
        int exp = 0;
        while (std::isdigit(*p)) {
            if (exp < 100000) exp = exp * 10 + (*p - '0');
            p++;
        }
        if (expNegative)
            exp = -exp;
        value *= std::pow(10, exp);
    }

    // 남은 문자가 없어야 함
    if (*p != '\0')
        return false;

    // 정확한 반올림은 C 런타임의 strtod 가 보장한다(MSVC UCRT/glibc/bionic 모두 올바른 반올림).
    // 소수점 기호가 '.' 인 로케일에서만 쓰고, 호스트가 다른 로케일을 걸었으면 classic 스트림으로 간다.
    // 리터럴마다 istringstream 을 만들고 imbue 하는 비용(약 2–3 µs)을 없앤다.
    const char* decimalPoint = std::localeconv()->decimal_point;
    if (decimalPoint != nullptr && decimalPoint[0] == '.' && decimalPoint[1] == '\0')
    {
        char* end = nullptr;
        const double precise = std::strtod(original, &end);
        if (end != original) value = std::fabs(precise);
    }
    else
    {
        std::istringstream decimal(original);
        decimal.imbue(std::locale::classic());
        double precise;
        if (decimal >> precise) value = std::fabs(precise);
    }
    r = negative ? -value : value;
    return true;
}

}
