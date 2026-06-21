# LG 2026 SW Architecture Studio Project — Time Grapher Project (Team 1)

## 📖 Architecture Documents

**→ [documents/README.md](documents/README.md)**


## 🔧 How to build on the Ubuntu or Raspberry Pi OS

```bash
sudo apt update
sudo apt install libglu1-mesa-dev libx11-xcb-dev build-essential cmake libasound2-dev

cmake -S . -B build -G Ninja
cmake --build build --target TimeGrapher
```