#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace NeoScript
{
// binary64 최댓값의 부호·정수 309자리·점·소수 9자리·종료 문자를 모두 담는다.
static const std::size_t FixedNumberBufferSize = 384;

namespace fixed_format_detail
{
// 가수에 5^decimals를 곱하고 2의 거듭제곱을 적용한다. 최대 1054비트이므로 힙 저장소가 필요 없다.
class Unsigned
{
    std::uint32_t words_[34];
    int count_;

    void Normalize() { while (count_ > 1 && words_[count_ - 1] == 0) --count_; }
    bool Bit(unsigned bit) const
    { return bit / 32 < static_cast<unsigned>(count_) && ((words_[bit / 32] >> (bit % 32)) & 1u) != 0; }
    bool Below(unsigned bit) const
    {
        const unsigned whole = bit / 32;
        for (unsigned i = 0; i < whole && i < static_cast<unsigned>(count_); ++i)
            if (words_[i] != 0) return true;
        return whole < static_cast<unsigned>(count_) && bit % 32 != 0 &&
            (words_[whole] & ((std::uint32_t(1) << (bit % 32)) - 1u)) != 0;
    }
    std::uint32_t DivideBillion()
    {
        std::uint64_t remainder = 0;
        for (int i = count_ - 1; i >= 0; --i)
        {
            const std::uint64_t value = (remainder << 32) | words_[i];
            words_[i] = static_cast<std::uint32_t>(value / 1000000000u);
            remainder = value % 1000000000u;
        }
        Normalize();
        return static_cast<std::uint32_t>(remainder);
    }

public:
    explicit Unsigned(std::uint64_t value) : count_(value >> 32 ? 2 : 1)
    { words_[0] = static_cast<std::uint32_t>(value); words_[1] = static_cast<std::uint32_t>(value >> 32); }
    bool Zero() const { return count_ == 1 && words_[0] == 0; }
    void Multiply(std::uint32_t scale)
    {
        std::uint64_t carry = 0;
        for (int i = 0; i < count_; ++i)
        {
            const std::uint64_t value = std::uint64_t(words_[i]) * scale + carry;
            words_[i] = static_cast<std::uint32_t>(value);
            carry = value >> 32;
        }
        if (carry) { assert(count_ < 34); words_[count_++] = static_cast<std::uint32_t>(carry); }
    }
    void ShiftLeft(unsigned shift)
    {
        const int whole = static_cast<int>(shift / 32), bits = static_cast<int>(shift % 32);
        const int previous = count_;
        count_ += whole + (bits != 0 ? 1 : 0);
        assert(count_ <= 34);
        for (int i = count_ - 1; i >= 0; --i)
        {
            const int from = i - whole;
            std::uint32_t value = from >= 0 && from < previous ? words_[from] << bits : 0;
            if (bits && from > 0 && from - 1 < previous) value |= words_[from - 1] >> (32 - bits);
            words_[i] = value;
        }
        Normalize();
    }
    void RoundRight(unsigned shift)
    {
        assert(shift != 0);
        // 버리는 최상위 비트가 1이면 절반 이상이다. 남은 비트가 없을 때만 몫의 홀짝으로 동점을 푼다.
        const bool up = Bit(shift - 1) && (Below(shift - 1) || Bit(shift));
        const unsigned whole = shift / 32, bits = shift % 32;
        if (whole >= static_cast<unsigned>(count_)) { count_ = 1; words_[0] = 0; }
        else
        {
            const int previous = count_;
            count_ -= static_cast<int>(whole);
            for (int i = 0; i < count_; ++i)
            {
                const int from = i + static_cast<int>(whole);
                words_[i] = words_[from] >> bits;
                if (bits && from + 1 < previous) words_[i] |= words_[from + 1] << (32 - bits);
            }
            Normalize();
        }
        if (up)
        {
            int i = 0;
            while (i < count_ && ++words_[i] == 0) ++i;
            if (i == count_) { assert(count_ < 34); words_[count_++] = 1; }
        }
    }
    std::size_t Decimal(char* output)
    {
        std::uint32_t chunks[36];
        int count = 0;
        do { assert(count < 36); chunks[count++] = DivideBillion(); } while (!Zero());
        char reversed[10];
        int digits = 0;
        std::uint32_t leading = chunks[--count];
        do { reversed[digits++] = static_cast<char>('0' + leading % 10); leading /= 10; } while (leading);
        char* cursor = output;
        while (digits) *cursor++ = reversed[--digits];
        while (count)
        {
            const std::uint32_t chunk = chunks[--count];
            for (std::uint32_t divisor = 100000000; divisor != 0; divisor /= 10)
                *cursor++ = static_cast<char>('0' + (chunk / divisor) % 10);
        }
        return static_cast<std::size_t>(cursor - output);
    }
};
}

// 유한한 값만 변환한다. NaN·무한대는 false를 돌려 호출자가 기존 tostring 표기를 재사용하게 한다.
// 부동소수 연산·로케일 API 없이 정수로 계산하므로 스레드의 반올림 모드와 FTZ 설정도 결과에 영향을 주지 않는다.
inline bool FormatFixedNumber(double value, unsigned decimals, char (&output)[FixedNumberBufferSize])
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
        "Fixed formatting requires IEEE binary64");
    assert(decimals <= 9);
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const unsigned exponent = static_cast<unsigned>((bits >> 52) & 0x7ffu);
    if (exponent == 0x7ffu) return false;
    const std::uint64_t fraction = bits & ((std::uint64_t(1) << 52) - 1u);
    fixed_format_detail::Unsigned integer(exponent ? fraction | (std::uint64_t(1) << 52) : fraction);
    static const std::uint32_t powersOfFive[] = { 1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125 };
    integer.Multiply(powersOfFive[decimals]);
    const int shift = (exponent ? static_cast<int>(exponent) - 1075 : -1074) + static_cast<int>(decimals);
    if (shift < 0) integer.RoundRight(static_cast<unsigned>(-shift));
    else integer.ShiftLeft(static_cast<unsigned>(shift));
    const bool negative = (bits >> 63) != 0 && !integer.Zero();
    char* digits = output + (negative ? 1 : 0);
    const std::size_t length = integer.Decimal(digits);
    std::size_t size = length;
    if (decimals)
    {
        if (length <= decimals)
        {
            const std::size_t zeros = decimals - length;
            std::memmove(digits + 2 + zeros, digits, length);
            digits[0] = '0'; digits[1] = '.';
            std::memset(digits + 2, '0', zeros);
            size = 2 + decimals;
        }
        else
        {
            const std::size_t whole = length - decimals;
            std::memmove(digits + whole + 1, digits + whole, decimals);
            digits[whole] = '.';
            size = length + 1;
        }
    }
    if (negative) output[0] = '-';
    digits[size] = '\0';
    return true;
}
}
