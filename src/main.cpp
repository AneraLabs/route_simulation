#include <iostream>
#include <vector>
#include <string>

using Amount = double;
using Ticks = uint64_t;

struct ChainParams
{
    const Amount orderflowRegenPerTick;
    const Amount outflowRegenPerTick;
    const Amount gasCost;
    const Amount executionSurplus;
    const Ticks bridgingTime;
    const Ticks inventoryLockTime;
};

struct Chain
{
    Chain(
        std::string&& name,
        ChainParams&& rp,
        Amount initialOrderflowBal,
        Amount initialOutflowBal,
        Amount startingStrategyBal)
        : chainName(std::move(name))
        , currentOrderflowBal(initialOrderflowBal)
        , currentOutflowBal(initialOutflowBal)
        , currentStrategyBal(startingStrategyBal)
        , maxOrderflowBal(currentOrderflowBal * 1.5)
        , maxOutflowBal(currentOutflowBal * 1.5)
        , maxStrategyBal(currentStrategyBal * 1.5)
        , params(std::move(rp))
        , balance(startingStrategyBal)
    { }

    const std::string chainName;
    
    Amount currentOrderflowBal;
    Amount currentOutflowBal;
    Amount currentStrategyBal;
    const Amount maxOrderflowBal;
    const Amount maxOutflowBal;
    const Amount maxStrategyBal;
    const ChainParams params;
    
    Amount balance;
    using LockedAmounts = std::vector<std::pair<Amount, Ticks>>;
    LockedAmounts lockedBalances;
};

using Chains = std::vector<Chain>;

struct Action
{
    enum type
    {
        bridge,
        execute
    } type;

    std::string source;
    std::string destination;
    Amount amount;
};

using Actions = std::vector<Action>;

class IStrategy
{
public:
    virtual ~IStrategy() = default;

    virtual void onTickRecalc(const Chains& chains, Actions& actions) = 0;
};

class Simulation
{
public:

    explicit Simulation(IStrategy* strategy)
        : m_strategy(strategy)
    {
        initRoutes();
    }

    ~Simulation() = default;

    void simulate(uint64_t iterations)
    {
        reportState();

        std::cout << "Starting simulation.." << std::endl;

        for (uint64_t t{ 0 }; t < iterations; ++t)
        {
            tick(t);
        }

        std::cout << "..finished." << std::endl;

        reportState();
    }

private:
    void initRoutes()
    {
        Chain chainA(
            "A",
            ChainParams{
                0.64,       // High order flow
                0.24,       // low bridging rate
                0.0001,     // low gas cost
                1.0005,     // 5 bips profitability
                4,          // medium bridging wait time ticks
                4           // low order execution wait time ticks
            },
            10,             // Initial order flow balance
            30,             // Initial bridge amount balance
            10              // Starting funds
        );

        Chain chainB(
            "B",
            ChainParams{
                0.38,       // medium order flow
                0.4,        // medium bridging rate
                0.0005,     // medium gas cost
                1.0003,     // 3 bips profitability
                6,          // high bridging wait time ticks
                6           // medium order execution wait time ticks
            },
            30,             // Initial order flow balance
            10,             // Initial bridge amount balance
            0
        );

        Chain chainC(
            "C",
            ChainParams{
                0.24,       // Low order flow
                0.61,       // high bridging rate
                0.0008,     // high gas cost
                1.0009,     // 9 bips profitability
                4,          // medium bridging wait time ticks
                8           // high order execution wait time ticks
            },
            40,             // Initial order flow balance
            30,             // Initial bridge amount balance
            0
        );

        m_chains.push_back(std::move(chainA));
        m_chains.push_back(std::move(chainB));
        m_chains.push_back(std::move(chainC));
    }

    void tick(uint64_t tickCounter)
    {
        if (tickCounter % 100 == 0)
        {
            std::cout << "... [" << tickCounter << "] ..." << std::endl;
        }

        // Tick pending balances and credit to balance if needed
        for (auto& chain : m_chains)
        {
            chain.currentOrderflowBal = std::min(
                chain.currentOrderflowBal + chain.params.orderflowRegenPerTick,
                chain.maxOrderflowBal);

            chain.currentOutflowBal = std::min(chain.currentOutflowBal + chain.params.outflowRegenPerTick,
                chain.maxOutflowBal);

            chain.lockedBalances.erase(
                std::remove_if(
                    chain.lockedBalances.begin(),
                    chain.lockedBalances.end(),
                    [&chain, &tickCounter](auto& pendingBal) {
                        auto& [balance, ticks] = pendingBal;
                        if (ticks > 0) {
                            --ticks;
                        }

                        if (ticks == 0) {
                            chain.balance += balance;
                            std::cout << "[" << tickCounter << "]: amount [" << balance << "] now available on "
                                      << "chain [" << chain.chainName << "]" << std::endl;
                            // Mark for removal
                            return true; 
                        }
                        return false;
                    }),
                chain.lockedBalances.end());
        }

        // Trigger the simualate method
        Actions actions;
        m_strategy->onTickRecalc(m_chains, actions);

        // Execution strategy actions
        for (const auto& action : actions)
        {
            if (action.source == action.destination)
            {
                std::cout << "[" << tickCounter << "]: !!! Failed to execute action, chains can't be the same" << std::endl;
                continue;
            }

            // get source + destination chains
            Chain* pSource{ nullptr };
            Chain* pDestination{ nullptr };

            for (auto& chain : m_chains)
            {
                if (chain.chainName == action.source)
                {
                    pSource = &chain;
                }
                else if (chain.chainName == action.destination)
                {
                    pDestination = &chain;
                }
            }

            if (!pSource || !pDestination)
            {
                std::cout << "[" << tickCounter << "]: !!! Failed to find chain, skipping action" << std::endl;
                continue;
            }

            // check balance
            if (pSource->balance < action.amount) {
                std::cout << "[" << tickCounter << "]: !!! Insufficient funds for action, skipping action" << std::endl;
                continue;
            }
            
            // Execute action if possible
            if (action.type == Action::type::bridge)
            {
                if (pDestination->currentOutflowBal < action.amount) {
                    std::cout << "[" << tickCounter << "]: !!! Insufficient funds for [bridge] action on destination, skipping action" << std::endl;
                    continue;
                }

                if (action.amount < pSource->params.gasCost) {
                    std::cout << "[" << tickCounter << "]: !!! Insufficient funds to pay for [bridge] action, skipping action" << std::endl;
                    continue;
                }

                const Amount bridgedAmount = action.amount - pSource->params.gasCost;
                // Destination bridging pool amount reduced
                pDestination->currentOutflowBal -= action.amount;
                // Source bridging pool amount increased
                pSource->currentOutflowBal += action.amount;
                // Strategy balance reduced
                pSource->balance -= action.amount;
                
                pDestination->lockedBalances.push_back({ bridgedAmount, pSource->params.bridgingTime });

                std::cout << "[" << tickCounter << "]: Bridged from [" << pSource->chainName << "] to "
                          << "[" << pDestination->chainName + "] amount [" << bridgedAmount << "] in "
                          << "[" << pSource->params.bridgingTime  << "] ticks" << std::endl;
            }
            else if (action.type == Action::type::execute)
            {
                if (pDestination->currentOrderflowBal < action.amount) {
                    std::cout << "[" << tickCounter << "]: !!! Insufficient destination funds for [execute] action, skipping action" << std::endl;
                    continue;
                }

                if (action.amount < pSource->params.gasCost) {
                    std::cout << "[" << tickCounter << "]: !!! Insufficient source funds to pay for [execute] action, skipping action" << std::endl;
                    continue;
                }

                const Amount amountAfterGasCost = action.amount - pSource->params.gasCost;
                const Amount creditedAmount = amountAfterGasCost * pSource->params.executionSurplus;

                // Reduce source chain order amount
                pDestination->currentOrderflowBal -= action.amount;
                
                // Strategy balance reduced
                pSource->balance -= action.amount;

                pDestination->lockedBalances.push_back({ creditedAmount, pSource->params.inventoryLockTime });

                std::cout << "[" << tickCounter << "]: Executed order on [" << pSource->chainName << "] "
                          << "credited on [" << pDestination->chainName + "] amount [" << creditedAmount << "] "
                          << "in [" << pSource->params.inventoryLockTime << "] ticks" << std::endl;
            }
        }
    }

    void reportState()
    {
        // output result of value changes
        Amount total{ 0. };

        for (auto& chain : m_chains)
        {
            Amount lockedTotal(0);
            for (auto& [locked, ticks] : chain.lockedBalances)
            {
                lockedTotal += locked;
            }
            std::cout << "Chain [" << chain.chainName << "] balance [" << chain.balance << "] + locked [" << lockedTotal << "]" << std::endl;
            total += chain.balance;
            total += lockedTotal;
        }

        std::cout << "Total : " << total << std::endl;
    }

    IStrategy* m_strategy;

    Chains m_chains;
};

/// Strategy implementation

class Strategy : public IStrategy
{
public:
    virtual void onTickRecalc(const Chains& chains, Actions& actions) override
    {
        const Chain* pChainA = getChain(chains, "A");
        const Chain* pChainB = getChain(chains, "B");
        const Chain* pChainC = getChain(chains, "C");

        // TODO return actions object with steps in this tick
        // e.g.1
        // To bridge 2 from A to B we would perform:
        
        if (   pChainA->balance > 2
            && pChainB->currentOutflowBal > 2)
        {
            actions.push_back(Action{ Action::type::bridge, "A", "B", 2 });
        }

        // e.g.2
        // To fill an order of 5 on chain B and being funded on A we would perform:
        if (   pChainB->balance > 5 
            && pChainB->currentOrderflowBal > 5 )
        {
            actions.push_back(Action{ Action::type::execute, "B", "A", 5 });
        }
    }

    const Chain* getChain(const Chains& chains, const std::string& name)
    {
        auto it = std::find_if(chains.begin(), chains.end(),
            [&name](const Chain& chain) {
                return chain.chainName == name;
            });

        if (it != chains.end()) {
            return &(*it);
        }
        return nullptr;
    }
};

int main()
{
    Strategy st;
    Simulation sim(reinterpret_cast<IStrategy*>(&st));
    sim.simulate(1000);
}
