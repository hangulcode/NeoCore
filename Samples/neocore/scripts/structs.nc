struct Position
{
    float x;
    float y;
};

struct Particle
{
    Position position;
    int age;
};

Particle Advance(Particle particle, float dt)
{
    particle.position.x += dt;
    particle.age += 1;
    return particle;
}

Particle particle = Particle(Position(1.0, 2.0), 0);
Particle snapshot = particle;
particle = Particle(Position(10.0, 20.0), 3);
particle.position.x = 12.0;
particle.position.y += 1.0;
particle = Advance(particle, 0.5);

print(particle.position.x);
print(snapshot.position.x);
print(particle.position.y);
print(particle.age);
