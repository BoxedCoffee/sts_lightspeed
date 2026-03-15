//
// Created by gamerpuppy on 7/11/2021.
//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "game/Shop.h"
#include "game/GameContext.h"
#include "game/Game.h"

using namespace sts;

static int libgdxRoundPositive(float x) {
    return static_cast<int>(x + 0.5f);
}

void Shop::setup(GameContext &gc) {
    setupCards(gc);
    setupRelics(gc);
    setupPotions(gc);

    if (gc.ascension >= 16) {
        applyDiscount(1.10f);
    }
    if (gc.hasRelic(RelicId::THE_COURIER)) {
        applyDiscount(0.80f);
    }
    if (gc.hasRelic(RelicId::MEMBERSHIP_CARD)) {
        applyDiscount(0.50f);
    }
    removeCost = getRemoveCost(gc);
}

void Shop::setupCards(GameContext &gc) {
    auto roll_rarity = [&]() -> CardRarity {
        return rollCardRarityShop(gc.cardRng, gc.cardRarityFactor);
    };

    auto get_from_pool = [&](CardRarity rarity, CardType type) -> CardId {
        std::vector<CardId> pool;

        const std::vector<CardId> *src = nullptr;
        if (rarity == CardRarity::COMMON) {
            src = &gc.commonCardPool;
        } else if (rarity == CardRarity::UNCOMMON) {
            src = &gc.uncommonCardPool;
        } else if (rarity == CardRarity::RARE) {
            src = &gc.rareCardPool;
        }

        if (src == nullptr) {
            return CardId::INVALID;
        }

        for (const CardId cid : *src) {
            if (getCardType(cid) != type) {
                continue;
            }
            pool.push_back(cid);
        }

        std::sort(pool.begin(), pool.end(), [](CardId a, CardId b) {
            return std::strcmp(getCardStringId(a), getCardStringId(b)) < 0;
        });

        if (pool.empty()) {
            return CardId::INVALID;
        }

        const int idx = gc.cardRng.random(static_cast<int>(pool.size()) - 1);
        return pool[idx];
    };

    auto draw_colored = [&](CardType type, CardId exclude) -> Card {
        CardId id;
        do {
            CardRarity rarity = roll_rarity();
            if (type == CardType::POWER) {
                if (rarity == CardRarity::COMMON) {
                    rarity = CardRarity::UNCOMMON;
                }
                if (rarity == CardRarity::UNCOMMON) {
                    id = get_from_pool(rarity, type);
                    if (id == CardId::INVALID) {
                        id = get_from_pool(CardRarity::RARE, type);
                    }
                } else {
                    id = get_from_pool(rarity, type);
                }
            } else {
                id = get_from_pool(rarity, type);
            }
        } while (id == CardId::INVALID || getCardColor(id) == CardColor::COLORLESS || id == exclude);
        return gc.previewObtainCard(Card(id));
    };

    auto draw_colorless = [&](CardRarity rarity) -> Card {
        std::vector<CardId> pool;
        const int groupSize = ColorlessRarityCardPool::getGroupSize(rarity);
        for (int i = 0; i < groupSize; ++i) {
            pool.push_back(ColorlessRarityCardPool::getCardAt(rarity, i));
        }
        std::sort(pool.begin(), pool.end(), [](CardId a, CardId b) {
            return std::strcmp(getCardStringId(a), getCardStringId(b)) < 0;
        });
        const int idx = gc.cardRng.random(static_cast<int>(pool.size()) - 1);
        return gc.previewObtainCard(Card(pool[idx]));
    };

    cards[0] = draw_colored(CardType::ATTACK, CardId::INVALID);
    cards[1] = draw_colored(CardType::ATTACK, cards[0].id);
    cards[2] = draw_colored(CardType::SKILL, CardId::INVALID);
    cards[3] = draw_colored(CardType::SKILL, cards[2].id);
    cards[4] = draw_colored(CardType::POWER, CardId::INVALID);

    cards[5] = draw_colorless(CardRarity::UNCOMMON);
    cards[6] = draw_colorless(CardRarity::RARE);

    for (int i = 0; i < 5; ++i) {
        const auto rarity = cards[i].getRarity();
        float tmpPrice = cardRarityPrices[static_cast<int>(rarity)] * gc.merchantRng.random(0.9f, 1.1f);
        prices[i] = static_cast<int>(tmpPrice);
    }

    prices[5] = cardRarityPrices[(int)CardRarity::UNCOMMON] * gc.merchantRng.random(0.9f, 1.1f) * 1.2f;
    prices[6] = cardRarityPrices[(int)CardRarity::RARE] * gc.merchantRng.random(0.9f, 1.1f) * 1.2f;

    int saleIdx = gc.merchantRng.random(4);
    prices[saleIdx] /= 2;
}

void Shop::setupRelics(GameContext &gc) {
    relics[0] = gc.returnRandomRelic(rollRelicTier(gc.merchantRng), true, false);
    relicPrice(0) = libgdxRoundPositive(getRelicBasePrice(relics[0]) * gc.merchantRng.random(0.95f, 1.05f));

    relics[1] = gc.returnRandomRelic(rollRelicTier(gc.merchantRng), true, false);
    relicPrice(1) = libgdxRoundPositive(getRelicBasePrice(relics[1]) * gc.merchantRng.random(0.95f, 1.05f));

    relics[2] = gc.returnRandomRelic(RelicTier::SHOP, true, false);
    relicPrice(2) = libgdxRoundPositive(getRelicBasePrice(relics[2]) * gc.merchantRng.random(0.95f, 1.05f));
}

void Shop::setupPotions(GameContext &gc) {
    for (int i = 0; i < 3; ++i) {
        potions[i] = returnRandomPotion(gc.potionRng, gc.cc);
        const auto rarity = potionRarities[(int)potions[i]];
        const int basePrice = potionRarityPrices[(int)rarity];
        potionPrice(i) = libgdxRoundPositive(basePrice * gc.merchantRng.random(0.95f, 1.05f));
    }
}

void Shop::applyDiscount(float factor) {
    for (int & price : prices) {
        price = libgdxRoundPositive(factor * static_cast<float>(price));
    }
}

void Shop::buyCard(GameContext &gc, int idx) {
    gc.deck.obtain(gc, cards[idx], 1);
    gc.loseGold(cardPrice(idx), true);

    if (gc.hasRelic(RelicId::THE_COURIER)) {
        if (idx >= 5) {
            // colorless card
            CardRarity rarity = gc.merchantRng.random() < COLORLESS_RARE_CHANCE ?
                    CardRarity::RARE : CardRarity::UNCOMMON;
            cards[idx] = gc.previewObtainCard(getColorlessCardFromPool(gc.cardRng, rarity));
            cardPrice(idx) = getNewCardPrice(gc, rarity, true);
        } else {
            CardRarity rarity = gc.rollCardRarity(Room::SHOP);
            cards[idx] = gc.previewObtainCard(getRandomClassCardOfRarity(gc.mathUtilRng, gc.cc, rarity));
            cardPrice(idx) = getNewCardPrice(gc, rarity, false);
        }

    } else {
        cardPrice(idx) = -1;
    }
}

void Shop::buyRelic(GameContext &gc, int idx) {
    const RelicId r = relics[idx];

    bool openedScreen = gc.obtainRelic(r);
    if (openedScreen) {
        gc.regainControlAction = [](GameContext &gc) {
            gc.screenState = ScreenState::SHOP_ROOM;
            gc.regainControlAction = [] (auto &gc) {
                gc.screenState = ScreenState::MAP_SCREEN;
            };
        };
    }

    gc.loseGold(relicPrice(idx), true);

    if (r == RelicId::MEMBERSHIP_CARD) {
        applyDiscount(MEMBERSHIP_CARD_FACTOR);
        removeCost = static_cast<int>(std::round(static_cast<float>(removeCost) * MEMBERSHIP_CARD_FACTOR));
    }

    if (gc.hasRelic(RelicId::THE_COURIER)) {
        relics[idx] = gc.returnRandomRelic(rollRelicTier(gc.merchantRng), true, false);
        getNewPrice(gc, getRelicBasePrice(relics[idx]));
    } else {
        relicPrice(idx) = -1;
    }

    if (isEggRelic(relics[idx])) {
        for (auto &c : cards) {
            c = gc.previewObtainCard(c);
        }
    }
}

void Shop::buyPotion(GameContext &gc, int idx) {
//    if (gc.hasRelic(RelicId::SOZU)) { // just dont call this with sozu or without enough slots
//        return;
//    }
    gc.obtainPotion(potions[idx]);
    gc.loseGold(potionPrice(idx), true);
    if (gc.hasRelic(RelicId::THE_COURIER)) {
        potions[idx] = returnRandomPotion(gc.potionRng, gc.cc);
        potionPrice(idx) = getNewPrice(gc, getPotionBaseCost(potions[idx]));
    } else {
        potionPrice(idx) = -1;
    }
}

void Shop::buyCardRemove(GameContext &gc) {
    gc.loseGold(removeCost, true);
    removeCost = -1;
    ++gc.shopRemoveCount;

    gc.regainControlAction = [=](GameContext &g) {
        g.screenState = ScreenState::SHOP_ROOM;
        g.regainControlAction = gc.regainControlAction;
    };

    gc.openCardSelectScreen(CardSelectScreenType::REMOVE, 1);
}

int &Shop::cardPrice(int idx) {
    return prices[idx];
}

int Shop::cardPrice(int idx) const {
    return prices[idx];
}

int &Shop::relicPrice(int idx) {
    return prices[7+idx];
}

int Shop::relicPrice(int idx) const {
    return prices[7+idx];
}

int &Shop::potionPrice(int idx) {
    return prices[10+idx];
}

int Shop::potionPrice(int idx) const {
    return prices[10+idx];
}

int Shop::getNewCardPrice(GameContext &gc, CardRarity rarity, bool colorless) {
    float price = static_cast<float>(cardRarityPrices[static_cast<int>(rarity)] * gc.merchantRng.random(0.9f, 1.1f));
    if (colorless) {
        price *= 1.2f;
    }
    if (gc.hasRelic(RelicId::THE_COURIER)) {
        price *= 0.8f;
    }
    if (gc.hasRelic(RelicId::THE_COURIER)) {
        price *= 0.5f;
    }
    return static_cast<int>(price);
}

int Shop::getNewPrice(GameContext &gc, int basePrice) {
    basePrice = static_cast<int>(std::round(gc.merchantRng.random(0.95f, 1.05f)));
    if (gc.hasRelic(RelicId::THE_COURIER)) {
        std::round(basePrice * COURIER_FACTOR);
    }
    if (gc.hasRelic(RelicId::MEMBERSHIP_CARD)) {
        std::round(basePrice * MEMBERSHIP_CARD_FACTOR);
    }
    return basePrice;
}

int Shop::getRemoveCost(const GameContext &gc) {
    int cost;
    if (gc.hasRelic(RelicId::SMILING_MASK)) {
        cost = SMILING_MASK_PRICE;
    } else {
        cost = BASE_REMOVE_PRICE+(REMOVE_PRICE_INCREASE*gc.shopRemoveCount);
    }

    if (gc.hasRelic(RelicId::THE_COURIER) && gc.hasRelic(RelicId::MEMBERSHIP_CARD)) {
        cost = libgdxRoundPositive(static_cast<float>(cost) * COURIER_FACTOR * MEMBERSHIP_CARD_FACTOR);

    } else if (gc.hasRelic(RelicId::THE_COURIER)) {
        cost = libgdxRoundPositive(static_cast<float>(cost) * COURIER_FACTOR);

    } else if (gc.hasRelic(RelicId::MEMBERSHIP_CARD)) {
        cost = libgdxRoundPositive(static_cast<float>(cost) * MEMBERSHIP_CARD_FACTOR);
    }
    return cost;
}

CardRarity Shop::rollCardRarityShop(Random &cardRng, int cardRarityAdjustment) {
    static constexpr int BASE_RARE_CHANCE = 9;
    static constexpr int BASE_UNCOMMON_CHANCE = 37;

    int roll = cardRng.random(99);
    roll += cardRarityAdjustment;

    if (roll < BASE_RARE_CHANCE) {
        return CardRarity::RARE;

    } else if (roll >= BASE_RARE_CHANCE + BASE_UNCOMMON_CHANCE) {\
        return CardRarity::COMMON;

    } else {
        return CardRarity::UNCOMMON;
    }
}

RelicTier Shop::rollRelicTier(Random &merchantRng) {
    int roll = merchantRng.random(99);
    if (roll < 48) {
        return RelicTier::COMMON;
    } else if (roll < 82) {
        return RelicTier::UNCOMMON;
    } else {
        return RelicTier::RARE;
    }
}

void Shop::assignRandomCardExcluding(GameContext &gc, CardType type, CardId excludeId, Card &outCard, CardRarity &outRarity) {
    CardId id;
    do {
        outRarity = rollCardRarityShop(gc.cardRng, 0);
        id = getRandomClassCardOfTypeAndRarity(gc.cardRng, gc.cc, type, outRarity);
    }while (id == excludeId);

    outCard = gc.previewObtainCard(id);
}
