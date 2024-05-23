In threadpool-primary, we implement Any class according to the underlying structure, 
semaphore class and Result class using condition variables and mutex, and finally realize the development of threadpool.

In threadpool-finally, according to the use of variable parameter template programming and reference folding principle provided in C++17, 
future class and packaged_task, the development of the final version of the threadpool is realized, the code is more lightweight.

When using primary, compile first `g++ -fPIC -shared threadpool.cpp -o libtdpool.so -std=c++17`

`g++ test.cpp -std=c++17 -ltdpool -lpthread`
