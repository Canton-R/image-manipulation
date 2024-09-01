#include "Limit.h"
#include "Book.h"

/**
 * @brief Constructs a new Limit object representing a price level in the order book.
 * @param limitPrice The price associated with this limit.
 */
Limit::Limit(int limitPrice)
    : limitPrice(limitPrice), size(0), totalVolume(0),
      headOrder(nullptr), tailOrder(nullptr) {}

/**
 * @brief Adds an order to this limit and updates the order book.
 * @param orderData The data associated with the order.
 * @param book Reference to the order book, used for updating the global order list.
 * @param newOrderId A unique order ID generated by an external IdSequence.
 */
void Limit::addOrderToLimit(const OrderData& orderData, Book& book, uint64_t newOrderId) {
    // This will create a new Order and add it to the Limit
    auto newOrder = std::make_unique<Order>(orderData, this, newOrderId);
    Order* newOrderPtr = newOrder.get();

    // Increment totalVolume and size for the Limit
    totalVolume += orderData.shares;
    size += 1;

    if (!headOrder) {
        // If this is the first order, set head and tail to this order
        headOrder = newOrderPtr;
        tailOrder = headOrder;
    } else {
        newOrderPtr->setPrevOrder(tailOrder); // Link the new order with the current tail
        tailOrder->setNextOrder(newOrderPtr); // Link the current tail with the new order
        tailOrder = newOrderPtr; // tailOrder now points to the new order
    }

    // Use book.addOrderToAllOrders to update the allOrders map
    book.addOrderToAllOrders(std::move(newOrder));
}

/**
 * @brief Processes the filling of an order against the current limit, either partially or fully, and updates the order book accordingly.
 * @param sharesTaker The number of shares in the incoming order that are being taken from the market.
 * @param takerSide The side of the incoming order (buy/sell).
 * @param orderId The ID of the incoming order that is attempting to take shares.
 * @param book Reference to the order book, used for managing global order and execution lists.
 */
void Limit::processFill(OrderData& takerData, uint64_t orderId, Book& book){
    
    Order* nxtOrder = headOrder;
    uint32_t takerClientId = takerData.clientId;
    Side takerSide = takerData.orderSide;
    
    while (nxtOrder != nullptr && size != 0 && takerData.shares != 0) {
        
        if (takerClientId == nxtOrder->getClientId()) {
            throw std::runtime_error("Invalid Order: two orders sent from the same client cannot match.");
        }
        int orderShares = nxtOrder->getShares();
        
        unsigned int executionVolume = std::min(orderShares, takerData.shares);

        logExecution(executionVolume, orderId, takerData, nxtOrder, book);

        totalVolume -= executionVolume;
        
        if (executionVolume >= orderShares) {
            takerData.shares -= orderShares;
            decreaseSize();
            nxtOrder = nxtOrder->getNextOrder();
            headOrder = nxtOrder;
        } else {
            nxtOrder->setShares(orderShares - executionVolume);
            takerData.shares -= executionVolume;
        }
    }
}


/**
 * @brief Logs the execution of a trade between a taker and a maker order.
 * @param executionVolume The volume of shares/contracts traded in this execution.
 * @param takerSide The side (Buy/Sell) of the taker order.
 * @param takerOrderId The unique ID of the taker order involved in the execution.
 * @param makerOrder Pointer to the maker order involved in the execution.
 * @param book Reference to the order book, used to add the new execution to the execution log.
 * @param remainingTakerShares The remaining shares of the taker order after this execution.
 */
void Limit::logExecution(int executionVolume, uint64_t takerOrderId, OrderData& takerData, Order* makerOrder, Book& book) {
    
    int remainingTakerShares = takerData.shares - executionVolume;
    int remainingMakerShares = makerOrder->getShares() - executionVolume;
    
    ExecutionType takerExecutionType = (remainingTakerShares == 0) ? ExecutionType::FullFill : ExecutionType::PartialFill;
    ExecutionType makerExecutionType = (executionVolume == makerOrder->getShares()) ? ExecutionType::FullFill : ExecutionType::PartialFill;
    
    int makerTotalExecQty = makerOrder->getExecutedQuantity() + executionVolume;
    int takerTotalExecQty = takerData.executedQuantity + executionVolume;
    
    makerOrder->setAvgPrice((makerOrder->getExecutedQuantity() * makerOrder->getAvgPrice() + executionVolume * makerOrder->getLimit()) / makerTotalExecQty);
    takerData.avgPrice = (takerData.executedQuantity * takerData.avgPrice + executionVolume * makerOrder->getLimit()) / takerTotalExecQty;
    
    makerOrder->setExecutedQuantity(makerTotalExecQty);
    takerData.executedQuantity = takerData.executedQuantity + executionVolume;
    
    auto execution = std::make_unique<Execution>(
        book.getSymbol(),
        book.getNextExecutionId(),
        makerOrder->getOrderId(),         // makerId (uint64_t)
        takerOrderId,                     // takerId (uint64_t)
        makerOrder->getLimit(),           // execPrice (double)
        executionVolume,                  // execSize (unsigned int)
        makerOrder->getOrderSide(),       // makerSide (Side enum)
        takerData.orderSide,              // takerSide (Side enum)
        makerExecutionType,               // makerExecType (ExecutionType enum)
        takerExecutionType,               // takerExecType (ExecutionType enum)
        takerData.clientId,
        makerOrder->getClientId(),
        makerTotalExecQty,
        takerTotalExecQty,
        remainingMakerShares,
        remainingTakerShares,
        makerOrder->getAvgPrice(),
        takerData.avgPrice
    );
    
    book.addExecutionToQueue(std::move(execution));
}

/**
 * @brief Decreases the size of the limit.
 */
void Limit::decreaseSize() {
    
    if (size > 0) {
        size -= 1;
    }
}

/**
 * @brief Returns the price level of this limit.
 * @return The limit price.
 */
int Limit::getLimitPrice() const {
    return limitPrice;
}

/**
 * @brief Returns the number of orders at this limit.
 * @return The number of orders (size).
 */
int Limit::getSize() const {
    return size;
}

/**
 * @brief Returns the total volume of shares at this limit.
 * @return The total volume of shares.
 */
int Limit::getTotalVolume() const {
    return totalVolume;
}

/**
 * @brief Returns the first order in the linked list at this limit.
 * @return Pointer to the head order.
 */
Order* Limit::getHeadOrder() const {
    return headOrder;
}

/**
 * @brief Returns the last order in the linked list at this limit.
 * @return Pointer to the tail order.
 */
Order* Limit::getTailOrder() const {
    return tailOrder;
}

/**
 * @brief Sets the total volume of shares at this limit.
 * @param newVolume The new total volume to set.
 */
void Limit::setTotalVolume(const int& newVolume) {
    totalVolume = newVolume;
}

/**
 * @brief Sets the last order in the linked list at this limit.
 * @param newTailOrder Pointer to the new tail order.
 */
void Limit::setTailOrder(Order* newTailOrder) {
    tailOrder = newTailOrder;
}

/**
 * @brief Sets the first order in the linked list at this limit.
 * @param newHeadOrder Pointer to the new head order.
 */
void Limit::setHeadOrder(Order* newHeadOrder) {
    headOrder = newHeadOrder;
}
