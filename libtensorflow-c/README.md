programming tensorflow in C

License
-------
[The MIT License (MIT)](https://opensource.org/licenses/MIT)


Dependencies
-------------

These dependencies are required:

    ----------------------------------------
    Library         |  Description
    ----------------|-----------------------
    libtensorflow   | download or build from source
    ----------------------------------------
    
### download libtensorflow-2.6.0

    $ cd $WORK_DIR
    $ wget https://storage.googleapis.com/tensorflow/libtensorflow/libtensorflow-gpu-linux-x86_64-2.6.0.tar.gz
    $ tar zxvf libtensorflow-gpu-linux-x86_64-2.6.0.tar.gz


### build from source
1. download google's building tools (bazel)
    
    // https://github.com/bazelbuild/bazel/releases/tag/4.2.2
    sudo mkdir /opt/google/bazel
    wget https://github.com/bazelbuild/bazel/releases/download/4.2.2/bazel-4.2.2-linux-x86_64 
    sudo mv bazel-4.2.2-linux-x86_64 /opt/google/bazel/
    sudo ln -s /top/google/bazel/bazel-4.2.2-linux-x86_64 /usr/local/bin/bazel

2. configure, build and install tensorflow library
##### build
    git clone https://github.com/tensorflow/tensorflow.git
    git checkout v2.8.2
    ./configure
    bazel build --jobs 4 \
        --local_ram_resources=HOST_RAM*0.5  \
        --config=opt --config monolithic \
        --config cuda \
        tensorflow/tools/lib_package:libtensorflow
    
#### install
	INSTALL_PREFIX="/opt/google/tensorflow"
	
    sudo mkdir -p ${INSTALL_PREFIX}/{lib,include/tensorflow/c}
    sudo cp -p bazel-bin/tensorflow/libtensorflow* ${INSTALL_PREFIX}/lib/
    sudo cp -rp bazel-bin/tensorflow/include/tensorflow/c/* ${INSTALL_PREFIX}/include/tensorflow/c/
    
    pushd $(pwd) 
    
    cd ${INSTALL_PREFIX}/lib
    sudo ln -s libtensorflow.so.2.8.2 libtensorflow.so.2
    sudo ln -s libtensorflow.so.2 libtensorflow.so
    
    popd


Test
-------------

    cd ${repo}/libtensorflow-c
### download model file
    
    mkdir models
    wget https://raw.githubusercontent.com/Neargye/hello_tf_c_api/master/models/graph.pb -O models/graph.pb
    
### compile and run tests
	./make.sh test
	
	export LD_LIBRARY_PATH=${INSTALL_PREFIX}/lib:/usr/local/cuda/lib64
	./test_tensorflow_context
	
	
