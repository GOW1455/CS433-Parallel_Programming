#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <omp.h>
#include <bits/stdc++.h>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " " << endl;
        return 1;
    }
    int n=atoi(argv[1]);
    string filename="A1_Q2_N="+to_string(n)+".txt";
    int size=2*n+1;

    vector<vector<double>> A(size,vector<double>(size,0.0));
    vector<double> x_true(size);
    vector<double> y(size,0.0);

    for (int i=0; i<size; ++i) {
        x_true[i]=((i%7)-3)*1.5+0.5;
    }

    for (int i=0; i<size; ++i) {
        int left=min(i,2*n-i);
        int right=max(i,2*n-i);
        double row_sum=0.0;

        for (int j=left; j<=right; ++j) {
            if (j==i) continue;

            if (j==2*n-i) {
                A[i][j]=((i+j)%5)+4.0;
            } else {
                A[i][j]=((i*j)%3)+1.0;
            }
            row_sum+=A[i][j];
        }
        A[i][i]=row_sum+((i+i)%5)+100.0;
    }

    for (int i=0; i<size; ++i) {
        for (int j=0; j<size; ++j) {
            y[i]+=A[i][j]*x_true[j];
        }
    }

    ofstream outfile(filename);
    if (!outfile.is_open()) {
        cerr<<"Error opening file: "<<filename<<endl;
        return 1;
    }
    
    outfile << n << "\n";
    for (int i=0; i<size; ++i) {
        int left=min(i,2*n-i);
        int right=max(i,2*n-i);
        for (int j=left; j<=right; ++j) {
            outfile<<fixed<<setprecision(6)<<A[i][j]
                   <<(j==right?"":" ");
        }
        outfile << "\n";
    }
    for (int i=0; i<size; ++i) {
        outfile<<fixed<<setprecision(6)<<y[i]<<(i==size-1?"":" ");
    }
    outfile << "\n";
    outfile.close();

    string x_filename="A1_Q2_N="+to_string(n)+"_x.txt";
    ofstream xfile(x_filename);
    if (!xfile.is_open()) {
        cerr<<"Error opening file: "<<x_filename<<endl;
        return 1;
    }

    xfile<<setprecision(15);
    for (int i=0; i<size; ++i)
        xfile<<x_true[i]<<(i==size-1?"":" ");
    xfile<<"\n";
    xfile.close();

    cout<<"Generated compressed input file "<<filename<<" for n="<<n<<endl;

    return 0;
}