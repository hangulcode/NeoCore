print("full match: " .. tostring("item42".regex_match("[a-z]+[0-9]+")));
print("whole string required: " .. tostring("x-item42".regex_match("[a-z]+[0-9]+")));
print("ignore case: " .. tostring("NEOCORE".regex_match("neocore", true)));

void SearchExamples()
{
    list<string> parts = "Order: item42".regex_search("([a-z]+)([0-9]+)");
    if (parts != null)
    {
        print("match: " .. parts[0]);
        print("name: " .. parts[1]);
        print("number: " .. parts[2]);
    }
    print("no match: " .. tostring("abc".regex_search("[0-9]+") == null));
    list<string> digits = "id=123".regex_search("\\d+");
    if (digits != null) print("digits: " .. digits[0]);
}
SearchExamples();
print("replace: " .. "a1 b2".regex_replace("([a-z])([0-9])", "$2$1"));
print("redact: " .. "id=123, score=456".regex_replace("[0-9]+", "#"));
print("empty match: " .. "ab".regex_replace("", "-"));
print("literal UTF-8: " .. tostring("한글".regex_match("한글")));
