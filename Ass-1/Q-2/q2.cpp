#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <omp.h>
#include <sys/time.h>
#include <iomanip>

using namespace std;

void InitializeInput(vector<vector<double>> &A, vector<double> &y, int n)
{
    int N = 2 * n + 1;

    A.clear();
    A.resize(N);
    y.assign(N, 0.0);

    vector<double> x_true(N, 0.0);

    for (int i = 0; i < N; ++i)
    {
        x_true[i] = ((i % 7) - 3) * 1.5 + 0.5;

        int left = min(i, 2 * n - i);
        int right = max(i, 2 * n - i);

        A[i].resize(right - left + 1, 0.0);

        double row_sum = 0.0;

        for (int j = left; j <= right; ++j)
        {
            if (j == i)
            {
                continue;
            }

            double value;

            if (j == 2 * n - i)
            {
                value = ((i + j) % 5) + 4.0;
            }
            else
            {
                value = ((i * j) % 3) + 1.0;
            }

            A[i][j - left] = value;
            row_sum += value;
        }

        A[i][i - left] = row_sum + ((i + i) % 5) + 100.0;
    }

    for (int i = 0; i < N; ++i)
    {
        int left = min(i, 2 * n - i);
        int right = max(i, 2 * n - i);

        for (int j = left; j <= right; ++j)
        {
            y[i] += A[i][j - left] * x_true[j];
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file> <num_threads>\n";
        return 1;
    }

    string input_file = argv[1];
    string output_file = argv[2];
    int num_threads = atoi(argv[3]);

    ifstream input(input_file);
    if (!input)
    {
        cerr << "Error opening input file: " << input_file << "\n";
        return 1;
    }

    int n;
    input >> n;

    int N = 2 * n + 1;
    vector<vector<double>> A(N);
    vector<double> y(N);
    vector<double> original_y(N);
    vector<double> x(N, 0.0);

    for (int row = 0; row < N; ++row)
    {
        int left = min(row, N - 1 - row);
        int right = max(row, N - 1 - row);

        A[row].resize(right - left + 1);

        for (double &value : A[row])
        {
            input >> value;
        }
    }

    for (int row = 0; row < N; ++row)
    {
        input >> y[row];
    }
    original_y = y;

    struct timeval tv0, tv1;
    struct timezone tz0, tz1;
    gettimeofday(&tv0, &tz0);

    x[n] = y[n] / A[n][0];

    #pragma omp parallel num_threads(num_threads)
    {
        #pragma omp for schedule(static)
        for (int k = 1; k <= n; ++k)
        {
            int l = n - k;
            int r = n + k;
            int offset = k;
            double *Al = A[l].data();
            double *Ar = A[r].data();

            y[l] -= Al[offset] * x[n];
            y[r] -= Ar[offset] * x[n];
        }

        int block_size = 10;

        for (int bs = 1; bs <= n; bs += block_size)
        {
            int be = min(n, bs + block_size - 1);

            #pragma omp single
            {
                for (int k = bs; k <= be; ++k)
                {
                    int l = n - k;
                    int r = n + k;
                    int ls = min(l, N - 1 - l);
                    int rs = min(r, N - 1 - r);
                    double *Al = A[l].data();
                    double *Ar = A[r].data();
                    double lhs = y[l];
                    double rhs = y[r];

                    for (int previous = bs; previous < k; ++previous)
                    {
                        int pl = n - previous;
                        int pr = n + previous;

                        lhs -= Al[pl - ls] * x[pl];
                        lhs -= Al[pr - ls] * x[pr];
                        rhs -= Ar[pl - rs] * x[pl];
                        rhs -= Ar[pr - rs] * x[pr];
                    }

                    double a = Al[l - ls];
                    double b = Al[r - ls];
                    double c = Ar[l - rs];
                    double d = Ar[r - rs];
                    double determinant = a * d - b * c;

                    x[l] = (lhs * d - b * rhs) / determinant;
                    x[r] = (a * rhs - c * lhs) / determinant;
                }
            }

            #pragma omp for schedule(dynamic, 32)
            for (int k = be + 1; k <= n; ++k)
            {
                int l = n - k;
                int r = n + k;
                int ls = min(l, N - 1 - l);
                int rs = min(r, N - 1 - r);
                double *Al = A[l].data();
                double *Ar = A[r].data();
                double lu = 0.0;
                double ru = 0.0;

                #pragma omp simd reduction(+:lu, ru)
                for (int current = bs; current <= be; ++current)
                {
                    int cl = n - current;
                    int cr = n + current;

                    lu += Al[cl - ls] * x[cl];
                    lu += Al[cr - ls] * x[cr];
                    ru += Ar[cl - rs] * x[cl];
                    ru += Ar[cr - rs] * x[cr];
                }

                y[l] -= lu;
                y[r] -= ru;
            }
        }
    }

    gettimeofday(&tv1, &tz1);

    long elapsed = (tv1.tv_sec - tv0.tv_sec) * 1000000L + (tv1.tv_usec - tv0.tv_usec);

    ofstream output(output_file);
    output << setprecision(17);
    for (double value : x)
    {
        output << value << " ";
    }
    output << "\n";

    double max_error = 0.0;
    for (int row = 0; row < N; ++row)
    {
        int left = min(row, N - 1 - row);
        int right = max(row, N - 1 - row);
        double ax = 0.0;

        for (int col = left; col <= right; ++col)
        {
            ax += A[row][col - left] * x[col];
        }

        max_error = max(max_error, fabs(ax - original_y[row]));
    }

    cout << "Time: " << elapsed << " microseconds\n";
    cout << "Max residual ||Ax - y||_inf: " << max_error << "\n";

    return 0;
}
