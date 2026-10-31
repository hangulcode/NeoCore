// 고정된 Node 값과 공유 리스트로 순환 그래프를 만든다.
// 이 샘플은 값의 생존과 명시적 연결 해제를 검증한다.
struct Node
{
    int tag;
    list<Node> links;
};

void Check(bool valid)
{
    if (!valid) { int zero = 0; print(1 / zero); }
}

void SelfCycle()
{
    list<Node> edges = [];
    Node node = Node(7, edges);
    edges.append(node);
    Check(edges[0].tag == 7 && edges[0].links.len() == 1);
    Node saved = edges[0];
    edges.clear();
    Check(saved.tag == 7 && saved.links.len() == 0);
}

void MutualCycle()
{
    list<Node> leftEdges = [];
    list<Node> rightEdges = [];
    Node left = Node(11, leftEdges);
    Node right = Node(22, rightEdges);
    leftEdges.append(right);
    rightEdges.append(left);
    Check(left.links[0].links[0].tag == 11);
    leftEdges.clear();
    Check(right.links[0].tag == 11 && right.links[0].links.len() == 0);
    rightEdges.clear();
}

void DetachedCycle()
{
    list<Node> edges = [];
    Node node = Node(3, edges);
    edges.append(node);
    Check(edges[0].links[0].tag == 3);
}

void ClosureCycle()
{
    list<fn()->int> callbacks = [];
    callbacks.append(fn()->int { return callbacks.len(); });
    Check(callbacks[0]() == 1);
    callbacks.clear();
    Check(callbacks.len() == 0);
}

export void CycleRun()
{
    for (int i in 0, 100)
    {
        SelfCycle();
        MutualCycle();
        DetachedCycle();
        ClosureCycle();
    }
    list<Node> edges = [];
    Node root = Node(0, edges);
    Node cursor = root;
    for (int i in 1, 513)
    {
        Node next = Node(i, []);
        cursor.links.append(next);
        cursor = next;
    }
    Check(root.links[0].tag == 1);
    cursor.links.append(root);
    Check(cursor.links[0].tag == 0);
    cursor.links.clear();
    print("cycle: typed graph and closure checks passed");
}
CycleRun();
