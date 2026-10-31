import system;

struct SaveData
{
    int version;
    string name;
    Vector3 position;
    list<int> inventory;
};

SaveData saved = SaveData(1, "player", Vector3(1.0, 2.0, 3.0), [7, 9]);
string text = system.jsonEncode(saved, true);
print(text);

SaveData loaded = system.jsonDecode<SaveData>(text);
print(loaded.name);
print(loaded.version);
print(loaded.position.z);
print(loaded.inventory[1]);

map<string, list<int>> groups = system.jsonDecode<map<string, list<int>>>("{\"coins\":[2,3,5]}");
list<int> coins = groups.get("coins", []);
print(coins[2]);

string absent = system.jsonDecode<string>("null");
print(system.jsonEncode(absent));

export bool CheckPassed()
{
    return loaded.version == 1 && loaded.name == "player" && loaded.position.z == 3.0
        && loaded.inventory[1] == 9 && coins[2] == 5 && absent == null;
}
