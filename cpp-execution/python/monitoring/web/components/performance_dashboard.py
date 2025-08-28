"""
Performance dashboard component for the crypto trading monitoring system.
Displays trading performance metrics, P&L, execution quality, and strategy performance.
"""

import requests
import pandas as pd
import numpy as np
from dash import dcc, html, dash_table, callback_context
import dash_bootstrap_components as dbc
from dash.dependencies import Input, Output, State
import plotly.graph_objs as go
import plotly.express as px
from datetime import datetime, timedelta

# Function to create the layout for the performance dashboard
def create_performance_layout(api_base_url):
    """Create the layout for the performance dashboard component."""
    return html.Div(
        [
            dbc.Row(
                [
                    dbc.Col(html.H2("Trading Performance", className="mb-4"), width=8),
                    dbc.Col(
                        dbc.Button(
                            "Refresh Performance Data", 
                            id="refresh-performance-button", 
                            color="primary", 
                            className="float-end"
                        ),
                        width=4,
                        className="d-flex justify-content-end",
                    ),
                ]
            ),
            
            # Summary cards
            dbc.Row(
                [
                    # Daily P&L
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Today's P&L", className="card-title"),
                                    html.Div(id="daily-pnl", className="mt-3 mb-3 d-flex justify-content-center"),
                                    html.Div(id="daily-pnl-change", className="text-center"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Weekly P&L
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Weekly P&L", className="card-title"),
                                    html.Div(id="weekly-pnl", className="mt-3 mb-3 d-flex justify-content-center"),
                                    html.Div(id="weekly-pnl-change", className="text-center"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Monthly P&L
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Monthly P&L", className="card-title"),
                                    html.Div(id="monthly-pnl", className="mt-3 mb-3 d-flex justify-content-center"),
                                    html.Div(id="monthly-pnl-change", className="text-center"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Execution Quality
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Execution Quality", className="card-title"),
                                    dcc.Graph(
                                        id="execution-quality-gauge",
                                        config={"displayModeBar": False},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                ]
            ),
            
            # P&L Charts
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("P&L Over Time", className="card-title"),
                                    dbc.Row(
                                        [
                                            dbc.Col(
                                                dbc.ButtonGroup(
                                                    [
                                                        dbc.Button("Day", id="day-pnl-btn", color="primary", outline=True, n_clicks=0),
                                                        dbc.Button("Week", id="week-pnl-btn", color="primary", outline=True, n_clicks=0),
                                                        dbc.Button("Month", id="month-pnl-btn", color="primary", outline=True, active=True, n_clicks=1),
                                                        dbc.Button("YTD", id="ytd-pnl-btn", color="primary", outline=True, n_clicks=0),
                                                    ],
                                                    className="mb-3",
                                                ),
                                                width=6,
                                            ),
                                            dbc.Col(
                                                dbc.RadioItems(
                                                    id="pnl-type-selector",
                                                    options=[
                                                        {"label": "Cumulative", "value": "cumulative"},
                                                        {"label": "Daily", "value": "daily"},
                                                    ],
                                                    value="cumulative",
                                                    inline=True,
                                                    className="float-end",
                                                ),
                                                width=6,
                                                className="d-flex justify-content-end align-items-center",
                                            ),
                                        ]
                                    ),
                                    dcc.Graph(
                                        id="pnl-chart",
                                        config={"displayModeBar": True},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Strategy Performance and Asset Distribution
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Strategy Performance", className="card-title"),
                                    dcc.Graph(
                                        id="strategy-performance-chart",
                                        config={"displayModeBar": True},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=6,
                    ),
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Asset Distribution", className="card-title"),
                                    dcc.Graph(
                                        id="asset-distribution-chart",
                                        config={"displayModeBar": True},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=6,
                    ),
                ]
            ),
            
            # Strategy Statistics Table
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Strategy Statistics", className="card-title"),
                                    html.Div(id="strategy-stats-table"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Execution Quality Details
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Execution Quality Details", className="card-title"),
                                    dbc.Tabs(
                                        [
                                            dbc.Tab(
                                                dbc.Card(
                                                    dbc.CardBody(
                                                        dcc.Graph(
                                                            id="slippage-chart",
                                                            config={"displayModeBar": True},
                                                        ),
                                                    ),
                                                    className="mt-3 border-0",
                                                ),
                                                label="Slippage",
                                                tab_id="slippage-tab",
                                            ),
                                            dbc.Tab(
                                                dbc.Card(
                                                    dbc.CardBody(
                                                        dcc.Graph(
                                                            id="fill-rate-chart",
                                                            config={"displayModeBar": True},
                                                        ),
                                                    ),
                                                    className="mt-3 border-0",
                                                ),
                                                label="Fill Rate",
                                                tab_id="fill-rate-tab",
                                            ),
                                            dbc.Tab(
                                                dbc.Card(
                                                    dbc.CardBody(
                                                        dcc.Graph(
                                                            id="execution-time-chart",
                                                            config={"displayModeBar": True},
                                                        ),
                                                    ),
                                                    className="mt-3 border-0",
                                                ),
                                                label="Execution Time",
                                                tab_id="execution-time-tab",
                                            ),
                                        ],
                                        id="execution-tabs",
                                        active_tab="slippage-tab",
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Hidden components for storing state
            dcc.Store(id="performance-data-json"),
            dcc.Store(id="strategy-data-json"),
            dcc.Store(id="execution-data-json"),
            dcc.Store(id="asset-data-json"),
            dcc.Store(id="pnl-timeframe", data="month"),
            
            # Interval component for auto-updates
            dcc.Interval(
                id="performance-interval-component",
                interval=60 * 1000,  # 60 seconds
                n_intervals=0,
            ),
        ]
    )

def register_performance_callbacks(app, api_base_url):
    """Register callbacks for the performance dashboard component."""
    
    # Callback to fetch performance data
    @app.callback(
        [
            Output("performance-data-json", "data"),
            Output("strategy-data-json", "data"),
            Output("execution-data-json", "data"),
            Output("asset-data-json", "data"),
        ],
        [
            Input("performance-interval-component", "n_intervals"),
            Input("refresh-performance-button", "n_clicks"),
            Input("pnl-timeframe", "data"),
        ],
    )
    def fetch_performance_data(n_intervals, n_clicks, timeframe):
        try:
            # Fetch P&L data
            pnl_response = requests.get(
                f"{api_base_url}/api/v1/trading/pnl?timeframe={timeframe}", 
                timeout=5
            )
            pnl_data = pnl_response.json() if pnl_response.status_code == 200 else {}
            
            # Fetch strategy performance data
            strategy_response = requests.get(
                f"{api_base_url}/api/v1/trading/strategy_performance", 
                timeout=5
            )
            strategy_data = strategy_response.json() if strategy_response.status_code == 200 else {}
            
            # Fetch execution quality data
            execution_response = requests.get(
                f"{api_base_url}/api/v1/trading/execution_quality", 
                timeout=5
            )
            execution_data = execution_response.json() if execution_response.status_code == 200 else {}
            
            # Fetch asset distribution data
            asset_response = requests.get(
                f"{api_base_url}/api/v1/trading/asset_distribution", 
                timeout=5
            )
            asset_data = asset_response.json() if asset_response.status_code == 200 else {}
            
            return pnl_data, strategy_data, execution_data, asset_data
            
        except Exception as e:
            print(f"Error fetching performance data: {e}")
            # Return empty data in case of error
            return {}, {}, {}, {}

    # Callback to update the P&L summary cards
    @app.callback(
        [
            Output("daily-pnl", "children"),
            Output("daily-pnl-change", "children"),
            Output("weekly-pnl", "children"),
            Output("weekly-pnl-change", "children"),
            Output("monthly-pnl", "children"),
            Output("monthly-pnl-change", "children"),
        ],
        [Input("performance-data-json", "data")]
    )
    def update_pnl_summaries(performance_data):
        if not performance_data:
            # Default values
            return (
                format_currency(0),
                format_change(0),
                format_currency(0),
                format_change(0),
                format_currency(0),
                format_change(0)
            )
            
        # Extract summary data
        summary = performance_data.get("summary", {})
        
        daily = summary.get("daily", {})
        weekly = summary.get("weekly", {})
        monthly = summary.get("monthly", {})
        
        # Create the output components
        daily_pnl = format_currency(daily.get("pnl", 0))
        daily_change = format_change(daily.get("change_percentage", 0))
        
        weekly_pnl = format_currency(weekly.get("pnl", 0))
        weekly_change = format_change(weekly.get("change_percentage", 0))
        
        monthly_pnl = format_currency(monthly.get("pnl", 0))
        monthly_change = format_change(monthly.get("change_percentage", 0))
        
        return (
            daily_pnl,
            daily_change,
            weekly_pnl,
            weekly_change,
            monthly_pnl,
            monthly_change
        )

    # Callback to update the execution quality gauge
    @app.callback(
        Output("execution-quality-gauge", "figure"),
        [Input("execution-data-json", "data")]
    )
    def update_execution_gauge(execution_data):
        if not execution_data:
            # Return empty gauge
            return create_empty_gauge("No Data")
            
        # Extract execution score
        score = execution_data.get("overall_score", 0)
        
        # Create a gauge figure
        fig = go.Figure(go.Indicator(
            mode="gauge+number",
            value=score,
            title={"text": "Quality Score"},
            gauge={
                "axis": {"range": [0, 100], "tickwidth": 1},
                "bar": {"color": get_quality_color(score)},
                "steps": [
                    {"range": [0, 50], "color": "rgba(255, 0, 0, 0.2)"},
                    {"range": [50, 80], "color": "rgba(255, 255, 0, 0.2)"},
                    {"range": [80, 100], "color": "rgba(0, 200, 0, 0.2)"}
                ],
                "threshold": {
                    "line": {"color": "white", "width": 4},
                    "thickness": 0.75,
                    "value": 80
                }
            }
        ))
        
        # Update layout for better fit
        fig.update_layout(
            height=150,
            margin=dict(l=20, r=20, t=30, b=20),
            paper_bgcolor="rgba(0,0,0,0)",
            plot_bgcolor="rgba(0,0,0,0)",
            font={"color": "white"}
        )
        
        return fig

    # Callback to update the P&L chart
    @app.callback(
        Output("pnl-chart", "figure"),
        [
            Input("performance-data-json", "data"),
            Input("pnl-type-selector", "value")
        ]
    )
    def update_pnl_chart(performance_data, pnl_type):
        if not performance_data or "data" not in performance_data:
            return create_empty_chart("No P&L data available")
            
        # Extract data
        data = performance_data.get("data", [])
        
        if not data:
            return create_empty_chart("No P&L data available")
            
        try:
            # Convert to DataFrame
            df = pd.DataFrame(data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Sort by timestamp
            df = df.sort_values('timestamp')
            
            # Convert to cumulative if needed
            if pnl_type == "cumulative":
                df['value'] = df['value'].cumsum()
            
            # Create figure
            fig = go.Figure()
            
            # Add P&L trace
            fig.add_trace(go.Scatter(
                x=df['timestamp'],
                y=df['value'],
                mode='lines',
                name='P&L',
                line=dict(
                    color='#2ecc71' if df['value'].iloc[-1] >= 0 else '#e74c3c',
                    width=2
                ),
                fill='tozeroy',
                fillcolor='rgba(46, 204, 113, 0.2)' if df['value'].iloc[-1] >= 0 else 'rgba(231, 76, 60, 0.2)'
            ))
            
            # Add zero line
            fig.add_hline(
                y=0,
                line_width=1,
                line_dash="dash",
                line_color="white",
                opacity=0.5
            )
            
            # Update layout
            title = "Cumulative P&L" if pnl_type == "cumulative" else "Daily P&L"
            fig.update_layout(
                title=title,
                xaxis_title="Date",
                yaxis_title="P&L (USD)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                hovermode="x unified"
            )
            
            # Format y-axis as currency
            fig.update_yaxes(tickprefix="$", tickformat=",.2f")
            
            return fig
        except Exception as e:
            print(f"Error creating P&L chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update strategy performance chart
    @app.callback(
        Output("strategy-performance-chart", "figure"),
        [Input("strategy-data-json", "data")]
    )
    def update_strategy_chart(strategy_data):
        if not strategy_data or "strategies" not in strategy_data:
            return create_empty_chart("No strategy data available")
            
        # Extract data
        strategies = strategy_data.get("strategies", [])
        
        if not strategies:
            return create_empty_chart("No strategy data available")
            
        try:
            # Prepare data for bar chart
            strategy_names = [s.get("name", "Unknown") for s in strategies]
            pnl_values = [s.get("pnl", 0) for s in strategies]
            sharpe_values = [s.get("sharpe_ratio", 0) for s in strategies]
            
            # Colors based on P&L
            colors = ['#2ecc71' if pnl >= 0 else '#e74c3c' for pnl in pnl_values]
            
            # Create figure with two subplots
            fig = go.Figure()
            
            # Add P&L bars
            fig.add_trace(go.Bar(
                x=strategy_names,
                y=pnl_values,
                name='P&L',
                marker_color=colors,
                text=[f"${pnl:,.2f}" for pnl in pnl_values],
                textposition='auto',
            ))
            
            # Add Sharpe ratio markers
            fig.add_trace(go.Scatter(
                x=strategy_names,
                y=sharpe_values,
                mode='markers',
                name='Sharpe Ratio',
                marker=dict(
                    size=12,
                    color='#f39c12',
                    symbol='diamond',
                    line=dict(
                        color='white',
                        width=1
                    )
                ),
                yaxis='y2'
            ))
            
            # Update layout with dual y-axes
            fig.update_layout(
                title="Strategy Performance",
                xaxis=dict(
                    title="Strategy",
                    titlefont_size=16,
                    tickangle=-45,
                ),
                yaxis=dict(
                    title="P&L (USD)",
                    titlefont_size=16,
                    tickprefix="$",
                    tickformat=",.2f",
                ),
                yaxis2=dict(
                    title="Sharpe Ratio",
                    titlefont_size=16,
                    overlaying="y",
                    side="right",
                ),
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=60, t=60, b=80),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=1.02,
                    xanchor="right",
                    x=1
                ),
                barmode='group'
            )
            
            return fig
        except Exception as e:
            print(f"Error creating strategy chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update asset distribution chart
    @app.callback(
        Output("asset-distribution-chart", "figure"),
        [Input("asset-data-json", "data")]
    )
    def update_asset_chart(asset_data):
        if not asset_data or "assets" not in asset_data:
            return create_empty_chart("No asset distribution data available")
            
        # Extract data
        assets = asset_data.get("assets", [])
        
        if not assets:
            return create_empty_chart("No asset distribution data available")
            
        try:
            # Prepare data for pie chart
            asset_names = [a.get("symbol", "Unknown") for a in assets]
            exposure_values = [a.get("exposure", 0) for a in assets]
            
            # Create figure
            fig = go.Figure(go.Pie(
                labels=asset_names,
                values=exposure_values,
                hole=.4,
                marker=dict(
                    colors=px.colors.qualitative.Plotly
                ),
                textinfo='label+percent',
                hoverinfo='label+value',
                hovertemplate='%{label}: $%{value:,.2f}<extra></extra>'
            ))
            
            # Update layout
            fig.update_layout(
                title="Asset Exposure Distribution",
                height=350,
                template="plotly_dark",
                margin=dict(l=20, r=20, t=60, b=20),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=-0.2,
                    xanchor="center",
                    x=0.5
                )
            )
            
            return fig
        except Exception as e:
            print(f"Error creating asset distribution chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update strategy statistics table
    @app.callback(
        Output("strategy-stats-table", "children"),
        [Input("strategy-data-json", "data")]
    )
    def update_strategy_table(strategy_data):
        if not strategy_data or "strategies" not in strategy_data:
            return html.P("No strategy statistics available", className="text-muted")
            
        # Extract data
        strategies = strategy_data.get("strategies", [])
        
        if not strategies:
            return html.P("No strategy statistics available", className="text-muted")
            
        # Create the table
        return dbc.Table(
            [
                html.Thead(
                    html.Tr([
                        html.Th("Strategy"),
                        html.Th("P&L"),
                        html.Th("Win Rate"),
                        html.Th("Sharpe Ratio"),
                        html.Th("Max Drawdown"),
                        html.Th("Trades"),
                        html.Th("Avg Trade"),
                    ])
                ),
                html.Tbody([
                    html.Tr([
                        html.Td(s.get("name", "Unknown")),
                        html.Td(
                            f"${s.get('pnl', 0):,.2f}", 
                            className="text-success" if s.get("pnl", 0) >= 0 else "text-danger"
                        ),
                        html.Td(f"{s.get('win_rate', 0) * 100:.1f}%"),
                        html.Td(f"{s.get('sharpe_ratio', 0):.2f}"),
                        html.Td(f"{s.get('max_drawdown', 0) * 100:.1f}%"),
                        html.Td(f"{s.get('trades', 0)}"),
                        html.Td(
                            f"${s.get('avg_trade', 0):,.2f}", 
                            className="text-success" if s.get("avg_trade", 0) >= 0 else "text-danger"
                        ),
                    ]) for s in strategies
                ])
            ],
            bordered=True,
            hover=True,
            responsive=True,
            striped=True,
            className="table-sm"
        )

    # Callbacks for execution quality tabs
    @app.callback(
        Output("slippage-chart", "figure"),
        [
            Input("execution-data-json", "data"),
            Input("execution-tabs", "active_tab"),
        ]
    )
    def update_slippage_chart(execution_data, active_tab):
        if active_tab != "slippage-tab" or not execution_data or "slippage" not in execution_data:
            return create_empty_chart("No slippage data available")
            
        # Extract data
        slippage_data = execution_data.get("slippage", [])
        
        if not slippage_data:
            return create_empty_chart("No slippage data available")
            
        try:
            # Prepare data
            df = pd.DataFrame(slippage_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add slippage trace
            fig.add_trace(go.Scatter(
                x=df['timestamp'],
                y=df['value'],
                mode='lines',
                name='Slippage',
                line=dict(color='#3498db', width=2),
            ))
            
            # Add average line
            avg_slippage = df['value'].mean()
            fig.add_hline(
                y=avg_slippage,
                line_width=2,
                line_dash="dash",
                line_color="#f39c12",
                annotation_text=f"Avg: {avg_slippage:.2f}bps",
                annotation_position="bottom right"
            )
            
            # Update layout
            fig.update_layout(
                title="Slippage Over Time",
                xaxis_title="Date",
                yaxis_title="Slippage (bps)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                hovermode="x unified"
            )
            
            return fig
        except Exception as e:
            print(f"Error creating slippage chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    @app.callback(
        Output("fill-rate-chart", "figure"),
        [
            Input("execution-data-json", "data"),
            Input("execution-tabs", "active_tab"),
        ]
    )
    def update_fill_rate_chart(execution_data, active_tab):
        if active_tab != "fill-rate-tab" or not execution_data or "fill_rate" not in execution_data:
            return create_empty_chart("No fill rate data available")
            
        # Extract data
        fill_rate_data = execution_data.get("fill_rate", [])
        
        if not fill_rate_data:
            return create_empty_chart("No fill rate data available")
            
        try:
            # Prepare data
            df = pd.DataFrame(fill_rate_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add fill rate trace
            fig.add_trace(go.Scatter(
                x=df['timestamp'],
                y=df['value'] * 100,  # Convert to percentage
                mode='lines',
                name='Fill Rate',
                line=dict(color='#2ecc71', width=2),
            ))
            
            # Add average line
            avg_fill_rate = df['value'].mean() * 100
            fig.add_hline(
                y=avg_fill_rate,
                line_width=2,
                line_dash="dash",
                line_color="#f39c12",
                annotation_text=f"Avg: {avg_fill_rate:.1f}%",
                annotation_position="bottom right"
            )
            
            # Update layout
            fig.update_layout(
                title="Fill Rate Over Time",
                xaxis_title="Date",
                yaxis_title="Fill Rate (%)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                hovermode="x unified"
            )
            
            # Format y-axis as percentage
            fig.update_yaxes(range=[0, 100])
            
            return fig
        except Exception as e:
            print(f"Error creating fill rate chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    @app.callback(
        Output("execution-time-chart", "figure"),
        [
            Input("execution-data-json", "data"),
            Input("execution-tabs", "active_tab"),
        ]
    )
    def update_execution_time_chart(execution_data, active_tab):
        if active_tab != "execution-time-tab" or not execution_data or "execution_time" not in execution_data:
            return create_empty_chart("No execution time data available")
            
        # Extract data
        execution_time_data = execution_data.get("execution_time", [])
        
        if not execution_time_data:
            return create_empty_chart("No execution time data available")
            
        try:
            # Prepare data
            df = pd.DataFrame(execution_time_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add execution time trace
            fig.add_trace(go.Scatter(
                x=df['timestamp'],
                y=df['value'],
                mode='lines',
                name='Execution Time',
                line=dict(color='#9b59b6', width=2),
            ))
            
            # Add average line
            avg_time = df['value'].mean()
            fig.add_hline(
                y=avg_time,
                line_width=2,
                line_dash="dash",
                line_color="#f39c12",
                annotation_text=f"Avg: {avg_time:.1f}ms",
                annotation_position="bottom right"
            )
            
            # Update layout
            fig.update_layout(
                title="Execution Time Over Time",
                xaxis_title="Date",
                yaxis_title="Execution Time (ms)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                hovermode="x unified"
            )
            
            return fig
        except Exception as e:
            print(f"Error creating execution time chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to handle timeframe buttons for P&L chart
    @app.callback(
        [
            Output("pnl-timeframe", "data"),
            Output("day-pnl-btn", "active"),
            Output("week-pnl-btn", "active"),
            Output("month-pnl-btn", "active"),
            Output("ytd-pnl-btn", "active"),
        ],
        [
            Input("day-pnl-btn", "n_clicks"),
            Input("week-pnl-btn", "n_clicks"),
            Input("month-pnl-btn", "n_clicks"),
            Input("ytd-pnl-btn", "n_clicks"),
        ],
        [State("pnl-timeframe", "data")]
    )
    def update_pnl_timeframe(btn_day, btn_week, btn_month, btn_ytd, current_timeframe):
        ctx = callback_context
        if not ctx.triggered:
            # Default to month view
            return "month", False, False, True, False
            
        button_id = ctx.triggered[0]["prop_id"].split(".")[0]
        timeframes = {
            "day-pnl-btn": "day",
            "week-pnl-btn": "week",
            "month-pnl-btn": "month",
            "ytd-pnl-btn": "ytd"
        }
        
        if button_id in timeframes:
            new_timeframe = timeframes[button_id]
            return (
                new_timeframe,
                new_timeframe == "day",
                new_timeframe == "week",
                new_timeframe == "month",
                new_timeframe == "ytd"
            )
        
        # Default case
        return current_timeframe, False, False, True, False

# Helper functions
def format_currency(value):
    """Format a value as currency with color based on sign."""
    color = "text-success" if value >= 0 else "text-danger"
    sign = "+" if value > 0 else ""
    return html.H3(f"{sign}${abs(value):,.2f}", className=color)

def format_change(percentage):
    """Format a percentage change with color and arrow."""
    if percentage == 0:
        return html.Span("0.0%", className="text-muted")
        
    color = "text-success" if percentage >= 0 else "text-danger"
    sign = "+" if percentage > 0 else ""
    arrow = "↑" if percentage > 0 else "↓"
    return html.Span(f"{arrow} {sign}{abs(percentage):.2f}%", className=color)

def get_quality_color(score):
    """Get color for execution quality score."""
    if score >= 80:
        return "green"
    elif score >= 50:
        return "yellow"
    else:
        return "red"

def create_empty_gauge(message):
    """Create an empty gauge figure with a message."""
    fig = go.Figure(go.Indicator(
        mode="gauge",
        gauge={"axis": {"range": [0, 100]}},
        title={"text": message}
    ))
    fig.update_layout(
        height=150,
        margin=dict(l=20, r=20, t=30, b=20),
        paper_bgcolor="rgba(0,0,0,0)",
        plot_bgcolor="rgba(0,0,0,0)",
        font={"color": "white"}
    )
    return fig

def create_empty_chart(message):
    """Create an empty chart with a message."""
    fig = go.Figure()
    fig.update_layout(
        title=message,
        height=350,
        template="plotly_dark",
        xaxis={"visible": False},
        yaxis={"visible": False},
        annotations=[
            {
                "text": message,
                "xref": "paper",
                "yref": "paper",
                "showarrow": False,
                "font": {
                    "size": 20
                }
            }
        ]
    )
    return fig