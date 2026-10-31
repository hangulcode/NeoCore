void Adjust(float& value, int& count) {
    value += 0.5;
    count += 1;
}

float value = 1.0;
int count = 0;
float& alias = value;
alias = 3.0;
Adjust(alias, count);
print(value);
print(count);
