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

void Advance(Particle& particle, float dt)
{
    particle.position.x += dt;
    particle.age += 1;
}

Particle particle = Particle(Position(1.0, 2.0), 0);
Particle snapshot = particle;
Position& position = particle.position;
float& x = particle.position.x;

particle = Particle(Position(10.0, 20.0), 3);
x = 12.0;
position.y += 1.0;
Advance(particle, 0.5);

print(particle.position.x);
print(snapshot.position.x);
print(particle.position.y);
print(particle.age);
