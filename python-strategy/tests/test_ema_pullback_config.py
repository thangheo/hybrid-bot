import yaml
from src.strategy.strategies.ema_pullback_with_imbalance import EMAPullbackImbalanceStrategy


def load_config(path='config/trading_bot.yaml'):
    with open(path, 'r') as f:
        return yaml.safe_load(f)


def test_init_with_full_config():
    cfg = load_config('config/trading_bot.yaml')
    strategy = EMAPullbackImbalanceStrategy(cfg)

    # Verify nested config was parsed
    assert hasattr(strategy, 'min_periods')
    assert strategy.min_periods == 50
    assert hasattr(strategy, 'enabled')
    # The default YAML shows enabled: false
    assert strategy.enabled is False

    # RSI ranges should be normalized into tuples of floats
    assert isinstance(strategy.rsi_buy_range, tuple)
    assert strategy.rsi_buy_range == (30.0, 50.0)


def test_init_with_section_only():
    cfg = load_config('config/trading_bot.yaml')['ema_pullback_with_imbalance']
    strategy = EMAPullbackImbalanceStrategy(cfg)

    assert strategy.min_periods == 50
    assert strategy.rsi_sell_range == (50.0, 70.0)


def test_defaults_when_missing():
    # Passing empty config should use defaults
    strategy = EMAPullbackImbalanceStrategy({})
    assert strategy.min_periods == 50  # default still 50 in this strategy
    assert strategy.ema_short == 20
    assert strategy.ema_long == 50
