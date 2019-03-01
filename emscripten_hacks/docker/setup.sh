apt update
apt install -y python
apt install -y git
apt install -y cmake
apt install -y nodejs
git clone https://github.com/juj/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest