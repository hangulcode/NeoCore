void First() { print("first"); }
void Second() { print("second"); }

fn()->void action = First;
fn()->void anonymous = fn()->void { print("anonymous function"); };
action();
anonymous();
action = Second;
action();
map<string, fn()->void> handlers = {};
handlers["action"] = First;
handlers.get("action", Second)();

fn()->int MakeCounter(int initial)
{
    int count = initial;
    return fn()->int { count += 1; return count; };
}
fn()->int counter = MakeCounter(10);
print(counter());
print(counter());
if (counter() != 13) { int zero = 0; print(1 / zero); }
