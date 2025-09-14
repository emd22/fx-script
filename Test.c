int Get2()
{
    return 2;
}

int Get5()
{
    return Get2() + 5;
}


int main()
{
    int x = Get5();
    int y = x + 2;
    return y;
}
