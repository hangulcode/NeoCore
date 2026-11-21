float bias = 0.0;

export void SetBias(array<float> input)
{
    bias = input[0];
}

export void Compute(array<float> input, array<float> values, array<int> indices)
{
    values.resize(1);
    indices.resize(1);
    values[0] = input[0] + bias;
    indices[0] = int(input[0]);
}
