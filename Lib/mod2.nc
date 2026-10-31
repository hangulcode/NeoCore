// 비공개 함수와 다른 모듈의 공개 함수를 함께 사용하는 예제다.
import mod1;

int addInternal(int a, int b)
{
    return a + b;
}

public int add(int a, int b)
{
    return addInternal(a, b);
}

public int sub(int a, int b)
{
    return a - b;
}

public int mul(int a, int b)
{
    return mod1.mul(a, b);
}
