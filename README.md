# Route Simulation

Route Simulation is a C++ project designed to simulate cross-chain order execution and bridging dynamics. It is designed to allow someone to develop strategies to maximize surplus from an initial starting balance.

The simulation offers a simplified representation of order flow on multiple chains, factoring in time penalties associated with executing orders or bridging.

## Code Overview

The entire project is contained within a single C++ file, which defines the following key components:

### Structs
- **ChainParams**: Structure defining the parameters for each chain, including order flow regeneration rate, bridging rate, gas cost, execution surplus, bridging time, and inventory lock time.
- **Chain**: Class representing a blockchain, holding balances and parameters.
- **Action**: Structure representing an action to be performed, such as bridging or executing an order.

### Interfaces
- **IStrategy**: Interface for strategy implementation.

### Classes
- **Simulation**: Class managing the simulation, executing actions based on the strategy, and updating chain states over iterations.
- **Strategy**: Example implementation of a strategy that decides actions to perform on each tick.

## How the simulation works

![image](https://github.com/user-attachments/assets/e19b527b-a8c3-4983-a55a-5c345718be64)

The Simulation class defines all the logic for updating the simulated state of the chains within each step (called a tick).

Within each tick the Strategy instance is called with a current snapshot of the state of the simulation and is given the opportunity to append actions which the Simulation executes before the tick completes.

## How to build a strategy

To build a custom Strategy, implement the IStrategy interface by defining the onTickRecalc method and pass this class into the simulation instance.

The onTickRecalc method receives the current state of all chains and allows a strategy author to decide the actions to take on each tick.

Examples are included in the placeholder strategy for accessing state and defining simulator actions.

For example, the following code will cause the strategy to bridge a value 2 from chain "A" to chain "B" :
```c++
actions.push_back(Action{ Action::type::bridge, "A", "B", 2 });
```

The following code will cause the strategy to fill an order of value 5 from chain "B" to chain "A". For cross chain orders the inventory is supplied on the destination chain (in this case "B") and refunded on the order source chain (in this case "A")
```c++
actions.push_back(Action{ Action::type::execute, "B", "A", 5 });
```

Strategies are judged on the sum of the balances on all chains after 1000 iterations.

## How to Compile

To build the project, you can use Visual Studio with the provided solution file or any C++ compiler that supports the C++17 standard. Below are the steps for building with a general C++ compiler:

1. Ensure you have a C++ compiler that supports C++17.
2. Compile the code with the following command:

```bash
   g++ -std=c++17 -o RouteSimulation main.cpp
```
