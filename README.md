# Trading App

## Description
This is a trading application developed as part of a university assignment. The application facilitates trading of various products among multiple traders.

## Features
- Parses product information from a file
- Parses trader information from command line arguments
- Connects traders to the exchange via FIFO pipes
- Matches buy and sell orders
- Reports order book and trader positions
- Handles order amendments and cancellations
- Collects exchange fees

## Installation
1. Clone the repository: `git clone https://github.com/your_username/trading-app.git`
2. Navigate to the project directory: `cd trading-app`
3. Compile the program: `gcc -o trading-app main.c pe_exchange.c -Wall -Wextra`

## Usage
1. Run the program with product file and trader executables as arguments:

2. Follow the prompts to execute trading operations.
