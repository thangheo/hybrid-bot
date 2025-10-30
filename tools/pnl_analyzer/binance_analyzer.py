#!/usr/bin/env python3
"""
Binance Trade Analyzer - Comprehensive P&L Calculator
Handles FIFO matching, fees, and unrealized gains
"""

import pandas as pd
import numpy as np
from datetime import datetime
from collections import defaultdict
import json

class BinanceTradeAnalyzer:
    """Analyzes Binance trade history with proper P&L calculation"""
    
    def __init__(self, csv_file):
        """Load and prepare trade data"""
        self.df = pd.read_csv(csv_file)
        self.df['Date(UTC)'] = pd.to_datetime(self.df['Date(UTC)'])
        self.df = self.df.sort_values('Date(UTC)')
        
        # Convert numeric columns
        for col in ['Price', 'Amount', 'Total', 'Fee']:
            self.df[col] = pd.to_numeric(self.df[col], errors='coerce')
        
        self.results = {}
        
    def calculate_pnl_fifo(self):
        """Calculate P&L using FIFO (First In First Out) method"""
        
        for pair in self.df['Pair'].unique():
            pair_df = self.df[self.df['Pair'] == pair].copy()
            
            buys = []  # Queue of buy orders
            trades_detail = []
            realized_pnl = 0
            total_buy_value = 0
            total_sell_value = 0
            total_sell_fees = 0
            
            for idx, row in pair_df.iterrows():
                if row['Type'] == 'BUY':
                    # Buy: fee is in the coin (you get less coin)
                    buys.append({
                        'date': row['Date(UTC)'],
                        'price': row['Price'],
                        'amount': row['Amount'],
                        'cost': row['Total'],
                        'fee_coin': row['Fee']
                    })
                    total_buy_value += row['Total']
                    
                    trades_detail.append({
                        'date': row['Date(UTC)'],
                        'type': 'BUY',
                        'price': row['Price'],
                        'amount': row['Amount'],
                        'total': row['Total'],
                        'pnl': None
                    })
                    
                elif row['Type'] == 'SELL':
                    # Sell: fee is in USDT (deducted from proceeds)
                    sell_amount = row['Amount']
                    gross_proceeds = row['Total']
                    fee_usdt = row['Fee']
                    net_proceeds = gross_proceeds - fee_usdt
                    
                    total_sell_value += gross_proceeds
                    total_sell_fees += fee_usdt
                    
                    # Match this sell with buys using FIFO
                    remaining_to_sell = sell_amount
                    trade_pnl = 0
                    
                    while remaining_to_sell > 0.0000001 and buys:
                        buy = buys[0]
                        
                        if buy['amount'] <= remaining_to_sell:
                            # Entire buy position is sold
                            matched_amount = buy['amount']
                            matched_cost = buy['cost']
                            matched_proceeds = (matched_amount / sell_amount) * net_proceeds
                            
                            pnl = matched_proceeds - matched_cost
                            realized_pnl += pnl
                            trade_pnl += pnl
                            
                            remaining_to_sell -= matched_amount
                            buys.pop(0)
                        else:
                            # Partial buy position is sold
                            matched_amount = remaining_to_sell
                            ratio = matched_amount / buy['amount']
                            matched_cost = buy['cost'] * ratio
                            matched_proceeds = (matched_amount / sell_amount) * net_proceeds
                            
                            pnl = matched_proceeds - matched_cost
                            realized_pnl += pnl
                            trade_pnl += pnl
                            
                            buy['amount'] -= matched_amount
                            buy['cost'] -= matched_cost
                            remaining_to_sell = 0
                    
                    trades_detail.append({
                        'date': row['Date(UTC)'],
                        'type': 'SELL',
                        'price': row['Price'],
                        'amount': sell_amount,
                        'total': gross_proceeds,
                        'pnl': trade_pnl
                    })
            
            # Calculate unrealized P&L for remaining positions
            unrealized_pnl = 0
            remaining_position = sum(b['amount'] for b in buys)
            remaining_cost = sum(b['cost'] for b in buys)
            
            if remaining_position > 0:
                last_price = pair_df.iloc[-1]['Price']
                current_value = remaining_position * last_price
                unrealized_pnl = current_value - remaining_cost
            
            self.results[pair] = {
                'trades': len(pair_df),
                'buys': len(pair_df[pair_df['Type'] == 'BUY']),
                'sells': len(pair_df[pair_df['Type'] == 'SELL']),
                'realized_pnl': realized_pnl,
                'unrealized_pnl': unrealized_pnl,
                'total_pnl': realized_pnl + unrealized_pnl,
                'buy_volume': total_buy_value,
                'sell_volume': total_sell_value,
                'sell_fees': total_sell_fees,
                'remaining_position': remaining_position,
                'remaining_cost': remaining_cost,
                'avg_buy_price': remaining_cost / remaining_position if remaining_position > 0 else 0,
                'last_price': pair_df.iloc[-1]['Price'],
                'trades_detail': trades_detail
            }
    
    def print_summary(self):
        """Print comprehensive summary"""
        print("="*80)
        print("BINANCE TRADE ANALYSIS")
        print("="*80)
        print(f"\nTotal Trades: {len(self.df)}")
        print(f"Date Range: {self.df['Date(UTC)'].min()} to {self.df['Date(UTC)'].max()}")
        print(f"Trading Pairs: {', '.join(self.df['Pair'].unique())}")
        print(f"\nBuys: {len(self.df[self.df['Type'] == 'BUY'])}")
        print(f"Sells: {len(self.df[self.df['Type'] == 'SELL'])}")
        
        print("\n" + "="*80)
        print("PROFIT/LOSS BY TRADING PAIR")
        print("="*80)
        
        # Sort by total P&L
        sorted_pairs = sorted(self.results.items(), 
                            key=lambda x: x[1]['total_pnl'], 
                            reverse=True)
        
        total_realized = 0
        total_unrealized = 0
        total_volume = 0
        
        for pair, stats in sorted_pairs:
            print(f"\n{pair}:")
            print(f"  Trades: {stats['trades']} ({stats['buys']} buys, {stats['sells']} sells)")
            print(f"  Buy Volume: ${stats['buy_volume']:,.2f}")
            print(f"  Sell Volume: ${stats['sell_volume']:,.2f}")
            print(f"  Realized P&L: ${stats['realized_pnl']:+,.2f}")
            print(f"  Unrealized P&L: ${stats['unrealized_pnl']:+,.2f}")
            print(f"  TOTAL P&L: ${stats['total_pnl']:+,.2f}")
            print(f"  Fees Paid: ${stats['sell_fees']:,.2f}")
            
            if stats['remaining_position'] > 0.0001:
                print(f"  Open Position: {stats['remaining_position']:.6f} @ ${stats['avg_buy_price']:.2f} avg")
                print(f"  Current Price: ${stats['last_price']:.2f}")
                gain_pct = ((stats['last_price'] - stats['avg_buy_price']) / stats['avg_buy_price'] * 100)
                print(f"  Unrealized Gain: {gain_pct:+.2f}%")
            
            total_realized += stats['realized_pnl']
            total_unrealized += stats['unrealized_pnl']
            total_volume += stats['buy_volume']
        
        print("\n" + "="*80)
        print("OVERALL SUMMARY")
        print("="*80)
        print(f"Total Trading Volume: ${total_volume:,.2f}")
        print(f"Total Realized P&L: ${total_realized:+,.2f}")
        print(f"Total Unrealized P&L: ${total_unrealized:+,.2f}")
        print(f"TOTAL P&L: ${total_realized + total_unrealized:+,.2f}")
        
        win_count = sum(1 for p, s in self.results.items() if s['total_pnl'] > 0)
        print(f"\nWin Rate: {win_count}/{len(self.results)} pairs profitable")
        
        # Trading frequency
        time_span = (self.df['Date(UTC)'].max() - self.df['Date(UTC)'].min()).total_seconds() / 3600
        trades_per_hour = len(self.df) / time_span if time_span > 0 else 0
        print(f"Trading Frequency: {trades_per_hour:.1f} trades/hour")
        
    def get_top_trades(self, n=10):
        """Get top N most profitable individual sells"""
        all_trades = []
        for pair, stats in self.results.items():
            for trade in stats['trades_detail']:
                if trade['pnl'] is not None:
                    all_trades.append({
                        'pair': pair,
                        'date': trade['date'],
                        'pnl': trade['pnl'],
                        'price': trade['price'],
                        'amount': trade['amount']
                    })
        
        sorted_trades = sorted(all_trades, key=lambda x: x['pnl'], reverse=True)
        
        print("\n" + "="*80)
        print(f"TOP {n} MOST PROFITABLE SELLS")
        print("="*80)
        for i, trade in enumerate(sorted_trades[:n], 1):
            print(f"{i}. {trade['pair']} - ${trade['pnl']:+,.2f} on {trade['date']}")
            print(f"   Sold {trade['amount']:.6f} @ ${trade['price']:.2f}")
        
        print("\n" + "="*80)
        print(f"TOP {n} WORST SELLS")
        print("="*80)
        for i, trade in enumerate(sorted_trades[-n:], 1):
            print(f"{i}. {trade['pair']} - ${trade['pnl']:+,.2f} on {trade['date']}")
            print(f"   Sold {trade['amount']:.6f} @ ${trade['price']:.2f}")
    
    def export_to_json(self, filename='trade_analysis.json'):
        """Export results to JSON"""
        export_data = {
            'summary': {
                'total_trades': len(self.df),
                'total_realized_pnl': sum(s['realized_pnl'] for s in self.results.values()),
                'total_unrealized_pnl': sum(s['unrealized_pnl'] for s in self.results.values()),
                'total_pnl': sum(s['total_pnl'] for s in self.results.values()),
                'date_range': {
                    'start': str(self.df['Date(UTC)'].min()),
                    'end': str(self.df['Date(UTC)'].max())
                }
            },
            'pairs': {}
        }
        
        for pair, stats in self.results.items():
            export_data['pairs'][pair] = {
                k: v for k, v in stats.items() 
                if k != 'trades_detail'
            }
        
        with open(filename, 'w') as f:
            json.dump(export_data, f, indent=2, default=str)
        
        print(f"\n✅ Results exported to {filename}")


def main():
    """Main execution"""
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python binance_analyzer.py <trade_history.csv>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    
    print("Loading trade data...")
    analyzer = BinanceTradeAnalyzer(csv_file)
    
    print("Calculating P&L using FIFO method...")
    analyzer.calculate_pnl_fifo()
    
    analyzer.print_summary()
    analyzer.get_top_trades(10)
    analyzer.export_to_json()


if __name__ == '__main__':
    main()
