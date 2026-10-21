#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

using namespace std;

// Passed 'per' by const reference to avoid unnecessary copying for large inputs
void generate(int num, int n, int a, int b, int c, const vector<int>& per, string filename)
{
    vector<string> ops(num);

    ofstream outfile(filename);
    if (!outfile.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        return;
    }

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> distrib(0, n - 1);

    for(int i = 0; i < num; i++)
    {
        int u = distrib(gen);
        int v = u;
        while(v == u)
        {
            v = distrib(gen);
        }
        
        if(i < (num * a) / 100)
        {
            ops[per[i]] = "insert " + to_string(u) + " " + to_string(v);
        }
        else if(i < (num * (a + b)) / 100)
        {
            ops[per[i]] = "delete " + to_string(u) + " " + to_string(v);
        }
        else
        {
            ops[per[i]] = "search " + to_string(u) + " " + to_string(v);
        }
    }
    
    for(int i = 0; i < num; i++)
    {
        outfile << ops[i] << "\n";
    }
    outfile.close();
}

int main(int argc, char* argv[])
{
    if(argc < 3)
    {
        cout << "Usage: " << argv[0] << " <no_of_nodes> <no_of_operations>" << endl;
        return 1;
    }
    int n = atoi(argv[1]);
    int num = atoi(argv[2]);

    string filename1 = "A2_Q2_N="+ to_string(n) +"_ops=" + to_string(num) + "_1.txt";
    string filename2 = "A2_Q2_N="+ to_string(n) +"_ops=" + to_string(num) + "_2.txt";
    string filename3 = "A2_Q2_N="+ to_string(n) +"_ops=" + to_string(num) + "_3.txt";

    vector<int> per(num);
    for(int i = 0; i < num; i++) per[i] = i;

    random_device rd;
    mt19937 g(rd());
    shuffle(per.begin(), per.end(), g);

    generate(num, n, 80, 10, 10, per, filename1);
    generate(num, n, 30, 30, 40, per, filename2);
    generate(num, n, 10, 10, 80, per, filename3);

    return 0;
}