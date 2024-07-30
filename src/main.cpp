#include <iostream>
#include <vector>
#include <string>

using Amount = double;
using Ticks = uint64_t;
using LockedAmounts = std::vector<std::pair<Amount, Ticks>>;

struct RouteParams
{
    const Amount orderflowRegenPerTick;
    const Amount outflowRegenPerTick;
    const Amount gasCost;
    const Amount executionSurplus;
    const Ticks bridgingTime;
    const Ticks inventoryLockTime;
};

struct Route
{
    Route() = delete;
    Route(std::string&& name, RouteParams&& rp, Amount initialOrderflowBal, Amount currentOutflowBal, Amount startingStrategyBal) 
        : currentOrderflowBal(initialOrderflowBal)
        , currentOutflowBal(currentOutflowBal)
        , currentStrategyBal(startingStrategyBal)
        , routeName(std::move(name))
        , params(std::move(rp))
    { }

    const std::string routeName;
    Amount currentOrderflowBal;
    Amount currentOutflowBal;
    Amount currentStrategyBal;
    const RouteParams params;
};

using Routes = std::vector<Route>;

struct Chain
{
    Chain(std::string&& name, Amount startingStrategyBal)
        : chainName(std::move(name))
        , balance(startingStrategyBal)
    { }

    const std::string chainName;
    Amount balance;
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

    virtual void onTickRecalc(const Routes& routes, const Chains& chains, Actions& actions) = 0;
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

        for (uint64_t t{ 0 }; t < iterations; ++t)
        {
            tick();
        }

        reportState();
    }

private:
    void initRoutes()
    {
        Route routeAB(
            "A->B",
            RouteParams{
                6.4,        // High order flow
                2.4,        // low bridging rate
                0.01,       // low gas cost
                1.0005,     // 5 bips profitability
                6,          // medium bridging wait time ticks
                2           // low order execution wait time ticks
            },
            20,
            20,
            0);

        // Medium order flow, medium bridging rate, medium gas cost
        Route routeBC(
            "B->C",
            RouteParams{
                3.8,        // medium order flow
                4,          // medium bridging rate
                0.05,       // medium gas cost
                1.0003,     // 3 bips profitability
                9,          // high bridging wait time ticks
                4           // medium order execution wait time ticks
            },
            30,
            10,
            0);

        // Low order flow, medium bridging rate, medium gas cost
        Route routeCA(
            "C->A",
            RouteParams{
                1.4,        // Low order flow
                6,          // high bridging rate
                0.08,       // high gas cost
                1.0009,     // 9 bips profitability
                4,          // medium bridging wait time ticks
                6           // high order execution wait time ticks
            },
            40,
            30,
            0);

        m_routes.push_back(std::move(routeAB));
        m_routes.push_back(std::move(routeBC));
        m_routes.push_back(std::move(routeCA));

        Chain chainA{ "A", 10 };
        Chain chainB{ "B", 10 };
        Chain chainC{ "C", 10 };

        m_chains.push_back(std::move(chainA));
        m_chains.push_back(std::move(chainB));
        m_chains.push_back(std::move(chainC));

    }

    void tick()
    {
        // Replenish values on each route
        for (auto& route : m_routes)
        {
            route.currentOrderflowBal += route.params.orderflowRegenPerTick;
            route.currentOutflowBal += route.params.outflowRegenPerTick;
        }

        // Trigger the simualate method
        Actions actions;
        m_strategy->onTickRecalc(m_routes, m_chains, actions);

        // Tick pending balances and credit to balance if needed
        for (auto& chain : m_chains)
        {
            chain.lockedBalances.erase(
                std::remove_if(
                    chain.lockedBalances.begin(),
                    chain.lockedBalances.end(),
                    [&chain](auto& pendingBal) {
                        auto& [balance, ticks] = pendingBal;
                        if (ticks > 0) {
                            --ticks;
                        }

                        if (ticks == 0) {
                            chain.balance += balance;
                            std::cout << "Amount [" << balance << "] now available on chain [" << chain.chainName << "]" << std::endl;
                            // Mark for removal
                            return true; 
                        }
                        return false;
                    }),
                chain.lockedBalances.end());
        }

        // Execution strategy actions
        for (const auto& action : actions)
        {
            // get source + destination chains
            Chain* pSource{ nullptr };
            Chain* pDestination{ nullptr };
            Route* pRoute{ nullptr };

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

            for (auto& route : m_routes)
            {
                const bool sourceMatches = route.routeName[0] == action.source[0];
                const bool destinationMatches = route.routeName[3] == action.destination[0];
                if (sourceMatches && destinationMatches) {
                    pRoute = &route;
                }
            }

            if (!pSource || !pDestination)
            {
                std::cout << "Failed to find chain, skipping action" << std::endl;
                continue;
            }

            if (!pRoute)
            {
                std::cout << "Failed to find route, skipping action" << std::endl;
                continue;
            }

            // check balance
            if (pSource->balance < action.amount) {
                std::cout << "Insufficient funds for action, skipping action" << std::endl;
                continue;
            }
            
            // Execute action if possible
            if (action.type == Action::type::bridge)
            {
                if (pRoute->currentOutflowBal < action.amount) {
                    std::cout << "Insufficient funds for bridge action, skipping action" << std::endl;
                    continue;
                }

                if (action.amount < pRoute->params.gasCost) {
                    std::cout << "Insufficient funds to pay for bridge action, skipping action" << std::endl;
                    continue;
                }

                Amount bridgedAmount = action.amount - pRoute->params.gasCost;
                pRoute->currentOutflowBal -= action.amount;
                pSource->balance -= action.amount;
                
                pDestination->lockedBalances.push_back({ bridgedAmount, pRoute->params.bridgingTime });

                std::cout << "Bridged from [" << pSource->chainName << "] to [" << pDestination->chainName + "] amount [" << bridgedAmount << "] in [" << pRoute->params.bridgingTime  << "] ticks" << std::endl;
            }
            else if (action.type == Action::type::execute)
            {
                if (pRoute->currentOrderflowBal < action.amount) {
                    std::cout << "Insufficient funds for execute action, skipping action" << std::endl;
                    continue;
                }

                if (action.amount < pRoute->params.gasCost) {
                    std::cout << "Insufficient funds to pay for execute action, skipping action" << std::endl;
                    continue;
                }

                Amount creditedAmount = (action.amount - pRoute->params.gasCost) * pRoute->params.executionSurplus;
                pRoute->currentOutflowBal -= action.amount;
                pSource->balance -= action.amount;

                pDestination->lockedBalances.push_back({ creditedAmount, pRoute->params.inventoryLockTime });

                std::cout << "Executed order on [" << pSource->chainName << "] credited on [" << pDestination->chainName + "] amount [" << creditedAmount << "] in [" << pRoute->params.inventoryLockTime << "]" << std::endl;
            }
        }
    }

    void reportState()
    {
        // output result of value changes
        for (auto& chain : m_chains)
        {
            Amount lockedTotal(0);
            for (auto& [locked, ticks] : chain.lockedBalances)
            {
                lockedTotal += locked;
            }
            std::cout << "Chain [" << chain.chainName << "] balance [" << chain.balance << "] + locked [" << lockedTotal  << "]" << std::endl;
        }
    }

    IStrategy* m_strategy;

    Routes m_routes;
    Chains m_chains;
};

/// Strategy implementation

class Strategy : public IStrategy
{
public:
    virtual void onTickRecalc(const Routes& routes, const Chains& chains, Actions& actions) override
    {
        // TODO return actions object with steps in this tick
        // e.g.
        // To brdige 10 from A to B we would perform:
        actions.push_back(Action{ Action::type::bridge, "A", "B", 10 });
    }
};

int main()
{
    Strategy st;
    Simulation sim(reinterpret_cast<IStrategy*>(&st));

    std::cout << "Starting simulation.." << std::endl;
    sim.simulate(10);
    std::cout << "..finished." << std::endl;
}
