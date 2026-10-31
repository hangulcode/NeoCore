import system;
int completed = 0;
bool correct = true;
void Got(bool ok, string response)
{
    correct = correct && ok && response == "typed http ok";
    completed += 1;
}
AsyncRequest request = system.async_create();
request.get(5000, "https://sample.invalid/echo", Got);
if (!request.wait()) correct = false;
request.close();
request = system.async_create();
request.add_header("Content-Type", "text/plain");
request.post(5000, "https://sample.invalid/echo", "typed http ok", Got);
if (!request.wait()) correct = false;
request.close();
export bool Verify() { return correct && completed == 2; }
