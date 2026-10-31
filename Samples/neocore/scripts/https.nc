import system;
void Got(bool ok, string response) { print(ok, response); }
AsyncRequest request = system.async_create();
request.get(5000, "https://example.com/", Got);
if (!request.wait()) print("request did not complete");
request.close();
