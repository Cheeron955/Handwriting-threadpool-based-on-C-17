In threadpool-primary, we implement Any class according to the underlying structure, 
semaphore class and Result class using condition variables and mutex, and finally realize the development of threadpool.

In threadpool-finally, according to the use of variable parameter template programming and reference folding principle provided in C++17, 
future class and packaged_task, the development of the final version of the threadpool is realized, the code is more lightweight.

When using primary, compile first `g++ -fPIC -shared threadpool.cpp -o libtdpool.so -std=c++17`

Put the .so library in the /usr/local/bin directory,.h in the /usr/local/include, write your own dynamic library configuration file

To compile test files, you need link libraries and thread libraries

`g++ test.cpp -std=c++17 -ltdpool -lpthread`

When using finally, compile first ，Put the threadpool.h in the /usr/local/include,

executive command `g++ ThreadPool.cpp -std=c++17 -lpthread`
