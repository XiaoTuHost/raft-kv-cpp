#!/bin/bash
echo "---> 开始编译protoc文件"
protoc --cpp_out=. *.proto

echo "---> 移动*.pb.h到当前目录的include目录"
if [ ! -d "./include/" ]; then
	echo "---> include目录不存在，创建include目录"
	mkdir include
fi
mv *.pb.h include
echo "---> 编译完成"