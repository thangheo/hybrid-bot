"""
Resource dashboard component for the crypto trading monitoring system.
Displays system resource metrics like CPU, memory, disk, and network usage.
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

# Function to create the layout for the resource dashboard
def create_resource_layout(api_base_url):
    """Create the layout for the resource dashboard component."""
    return html.Div(
        [
            dbc.Row(
                [
                    dbc.Col(html.H2("System Resources", className="mb-4"), width=8),
                    dbc.Col(
                        dbc.Button(
                            "Refresh Resource Data", 
                            id="refresh-resource-button", 
                            color="primary", 
                            className="float-end"
                        ),
                        width=4,
                        className="d-flex justify-content-end",
                    ),
                ]
            ),
            
            # Resource Overview Cards
            dbc.Row(
                [
                    # CPU Usage
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("CPU Usage", className="card-title"),
                                    dcc.Graph(
                                        id="cpu-gauge",
                                        config={"displayModeBar": False},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Memory Usage
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Memory Usage", className="card-title"),
                                    dcc.Graph(
                                        id="memory-gauge",
                                        config={"displayModeBar": False},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Disk Usage
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Disk Usage", className="card-title"),
                                    dcc.Graph(
                                        id="disk-gauge",
                                        config={"displayModeBar": False},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                    # Network Usage
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Network Traffic", className="card-title"),
                                    html.Div(id="network-usage-indicator", className="d-flex justify-content-center"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=3,
                    ),
                ]
            ),
            
            # Component Filtering
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Filter Components", className="card-title"),
                                    dcc.Dropdown(
                                        id="resource-component-filter",
                                        options=[{"label": "All Components", "value": "all"}],
                                        value="all",
                                        clearable=False,
                                        className="mb-3"
                                    ),
                                    dbc.ButtonGroup(
                                        [
                                            dbc.Button("1 Hour", id="1h-resource-btn", color="primary", outline=True, n_clicks=0),
                                            dbc.Button("6 Hours", id="6h-resource-btn", color="primary", outline=True, n_clicks=0),
                                            dbc.Button("24 Hours", id="24h-resource-btn", color="primary", outline=True, active=True, n_clicks=1),
                                            dbc.Button("7 Days", id="7d-resource-btn", color="primary", outline=True, n_clicks=0),
                                        ],
                                        className="mb-3",
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Resource Utilization Charts
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("CPU Utilization Over Time", className="card-title"),
                                    dcc.Graph(
                                        id="cpu-chart",
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
                                    html.H4("Memory Utilization Over Time", className="card-title"),
                                    dcc.Graph(
                                        id="memory-chart",
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
            
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Disk I/O Over Time", className="card-title"),
                                    dcc.Graph(
                                        id="disk-io-chart",
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
                                    html.H4("Network Traffic Over Time", className="card-title"),
                                    dcc.Graph(
                                        id="network-chart",
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
            
            # Top Resource Consumers
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Top Resource Consumers", className="card-title"),
                                    html.Div(id="resource-consumers-table"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Resource Alerts and Predictions
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Resource Alerts", className="card-title"),
                                    html.Div(id="resource-alerts"),
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
                                    html.H4("Resource Predictions", className="card-title"),
                                    html.Div(id="resource-predictions"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=6,
                    ),
                ]
            ),
            
            # Hidden components for storing state
            dcc.Store(id="resource-data-json"),
            dcc.Store(id="resource-timeframe", data="24h"),
            
            # Interval component for auto-updates
            dcc.Interval(
                id="resource-interval-component",
                interval=30 * 1000,  # 30 seconds
                n_intervals=0,
            ),
        ]
    )

def register_resource_callbacks(app, api_base_url):
    """Register callbacks for the resource dashboard component."""
    
    # Callback to fetch resource data
    @app.callback(
        Output("resource-data-json", "data"),
        [
            Input("resource-interval-component", "n_intervals"),
            Input("refresh-resource-button", "n_clicks"),
            Input("resource-timeframe", "data"),
            Input("resource-component-filter", "value"),
        ],
    )
    def fetch_resource_data(n_intervals, n_clicks, timeframe, component):
        try:
            # Build query parameters
            params = {
                "timeframe": timeframe
            }
            if component != "all":
                params["component"] = component
                
            # Fetch resource data
            resource_response = requests.get(
                f"{api_base_url}/api/v1/metrics/resources", 
                params=params,
                timeout=5
            )
            resource_data = resource_response.json() if resource_response.status_code == 200 else {}
            
            return resource_data
            
        except Exception as e:
            print(f"Error fetching resource data: {e}")
            # Return empty data in case of error
            return {}

    # Callback to update component filter dropdown
    @app.callback(
        Output("resource-component-filter", "options"),
        [Input("resource-data-json", "data")]
    )
    def update_component_filter(resource_data):
        if not resource_data or "components" not in resource_data:
            return [{"label": "All Components", "value": "all"}]
            
        # Extract unique components
        components = set()
        for component in resource_data.get("components", []):
            name = component.get("name")
            if name:
                components.add(name)
                
        # Create options list
        options = [{"label": "All Components", "value": "all"}]
        options.extend([{"label": comp, "value": comp} for comp in sorted(components)])
        
        return options

    # Callback to update CPU gauge
    @app.callback(
        Output("cpu-gauge", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_cpu_gauge(resource_data):
        if not resource_data or "system" not in resource_data:
            # Return empty gauge
            return create_empty_gauge("No Data")
            
        # Extract CPU data
        system = resource_data.get("system", {})
        cpu_usage = system.get("cpu_usage", 0)
        
        # Create gauge
        fig = create_gauge_figure(
            value=cpu_usage,
            title="CPU Usage",
            suffix="%",
            thresholds=[60, 80]
        )
        
        return fig

    # Callback to update memory gauge
    @app.callback(
        Output("memory-gauge", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_memory_gauge(resource_data):
        if not resource_data or "system" not in resource_data:
            # Return empty gauge
            return create_empty_gauge("No Data")
            
        # Extract memory data
        system = resource_data.get("system", {})
        memory_usage = system.get("memory_usage", 0)
        
        # Create gauge
        fig = create_gauge_figure(
            value=memory_usage,
            title="Memory Usage",
            suffix="%",
            thresholds=[70, 90]
        )
        
        return fig

    # Callback to update disk gauge
    @app.callback(
        Output("disk-gauge", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_disk_gauge(resource_data):
        if not resource_data or "system" not in resource_data:
            # Return empty gauge
            return create_empty_gauge("No Data")
            
        # Extract disk data
        system = resource_data.get("system", {})
        disk_usage = system.get("disk_usage", 0)
        
        # Create gauge
        fig = create_gauge_figure(
            value=disk_usage,
            title="Disk Usage",
            suffix="%",
            thresholds=[75, 90]
        )
        
        return fig

    # Callback to update network usage indicator
    @app.callback(
        Output("network-usage-indicator", "children"),
        [Input("resource-data-json", "data")]
    )
    def update_network_indicator(resource_data):
        if not resource_data or "system" not in resource_data:
            return html.Div(
                html.I(className="fas fa-question-circle fa-3x text-secondary"),
                className="text-center mt-3"
            )
            
        # Extract network data
        system = resource_data.get("system", {})
        network_in = system.get("network_in", 0)  # in KB/s
        network_out = system.get("network_out", 0)  # in KB/s
        
        # Format network rates
        in_formatted = format_network_rate(network_in)
        out_formatted = format_network_rate(network_out)
        
        # Create indicator
        return html.Div(
            [
                html.Div(
                    [
                        html.I(className="fas fa-arrow-down text-info me-2"),
                        html.Span(in_formatted, className="h4"),
                    ],
                    className="d-flex align-items-center justify-content-center mb-2"
                ),
                html.Div(
                    [
                        html.I(className="fas fa-arrow-up text-warning me-2"),
                        html.Span(out_formatted, className="h4"),
                    ],
                    className="d-flex align-items-center justify-content-center"
                ),
            ],
            className="mt-3"
        )

    # Callback to update CPU chart
    @app.callback(
        Output("cpu-chart", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_cpu_chart(resource_data):
        if not resource_data or "metrics" not in resource_data or "cpu" not in resource_data["metrics"]:
            return create_empty_chart("No CPU data available")
            
        # Extract data
        cpu_data = resource_data["metrics"]["cpu"]
        
        try:
            # Convert to DataFrame
            df = pd.DataFrame(cpu_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add trace for each component
            for component in df['component'].unique():
                component_df = df[df['component'] == component]
                fig.add_trace(go.Scatter(
                    x=component_df['timestamp'],
                    y=component_df['value'],
                    mode='lines',
                    name=component
                ))
                
            # Add threshold lines
            fig.add_hline(
                y=80,
                line_width=1,
                line_dash="dash",
                line_color="red",
                annotation_text="Critical",
                annotation_position="bottom right"
            )
            
            fig.add_hline(
                y=60,
                line_width=1,
                line_dash="dash",
                line_color="orange",
                annotation_text="Warning",
                annotation_position="bottom right"
            )
                
            # Update layout
            fig.update_layout(
                title="CPU Utilization",
                xaxis_title="Time",
                yaxis_title="CPU Usage (%)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=1.02,
                    xanchor="right",
                    x=1
                ),
                hovermode="x unified"
            )
            
            # Set y-axis range
            fig.update_yaxes(range=[0, 100])
            
            return fig
        except Exception as e:
            print(f"Error creating CPU chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update memory chart
    @app.callback(
        Output("memory-chart", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_memory_chart(resource_data):
        if not resource_data or "metrics" not in resource_data or "memory" not in resource_data["metrics"]:
            return create_empty_chart("No memory data available")
            
        # Extract data
        memory_data = resource_data["metrics"]["memory"]
        
        try:
            # Convert to DataFrame
            df = pd.DataFrame(memory_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add trace for each component
            for component in df['component'].unique():
                component_df = df[df['component'] == component]
                fig.add_trace(go.Scatter(
                    x=component_df['timestamp'],
                    y=component_df['value'],
                    mode='lines',
                    name=component
                ))
                
            # Add threshold lines
            fig.add_hline(
                y=90,
                line_width=1,
                line_dash="dash",
                line_color="red",
                annotation_text="Critical",
                annotation_position="bottom right"
            )
            
            fig.add_hline(
                y=70,
                line_width=1,
                line_dash="dash",
                line_color="orange",
                annotation_text="Warning",
                annotation_position="bottom right"
            )
                
            # Update layout
            fig.update_layout(
                title="Memory Utilization",
                xaxis_title="Time",
                yaxis_title="Memory Usage (%)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=1.02,
                    xanchor="right",
                    x=1
                ),
                hovermode="x unified"
            )
            
            # Set y-axis range
            fig.update_yaxes(range=[0, 100])
            
            return fig
        except Exception as e:
            print(f"Error creating memory chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update disk I/O chart
    @app.callback(
        Output("disk-io-chart", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_disk_io_chart(resource_data):
        if not resource_data or "metrics" not in resource_data or "disk_io" not in resource_data["metrics"]:
            return create_empty_chart("No disk I/O data available")
            
        # Extract data
        disk_io_data = resource_data["metrics"]["disk_io"]
        
        try:
            # Convert to DataFrame
            df = pd.DataFrame(disk_io_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Create separate DataFrames for read and write
            read_df = df[df['operation'] == 'read']
            write_df = df[df['operation'] == 'write']
            
            # Group by timestamp and component, then calculate average
            if not read_df.empty:
                read_avg = read_df.groupby(['timestamp', 'component']).agg({'value': 'mean'}).reset_index()
                for component in read_avg['component'].unique():
                    component_df = read_avg[read_avg['component'] == component]
                    fig.add_trace(go.Scatter(
                        x=component_df['timestamp'],
                        y=component_df['value'],
                        mode='lines',
                        name=f"{component} Read",
                        line=dict(dash='solid')
                    ))
            
            if not write_df.empty:
                write_avg = write_df.groupby(['timestamp', 'component']).agg({'value': 'mean'}).reset_index()
                for component in write_avg['component'].unique():
                    component_df = write_avg[write_avg['component'] == component]
                    fig.add_trace(go.Scatter(
                        x=component_df['timestamp'],
                        y=component_df['value'],
                        mode='lines',
                        name=f"{component} Write",
                        line=dict(dash='dash')
                    ))
                
            # Update layout
            fig.update_layout(
                title="Disk I/O",
                xaxis_title="Time",
                yaxis_title="I/O (MB/s)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=1.02,
                    xanchor="right",
                    x=1
                ),
                hovermode="x unified"
            )
            
            return fig
        except Exception as e:
            print(f"Error creating disk I/O chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update network chart
    @app.callback(
        Output("network-chart", "figure"),
        [Input("resource-data-json", "data")]
    )
    def update_network_chart(resource_data):
        if not resource_data or "metrics" not in resource_data or "network" not in resource_data["metrics"]:
            return create_empty_chart("No network data available")
            
        # Extract data
        network_data = resource_data["metrics"]["network"]
        
        try:
            # Convert to DataFrame
            df = pd.DataFrame(network_data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Create separate DataFrames for in and out
            in_df = df[df['direction'] == 'in']
            out_df = df[df['direction'] == 'out']
            
            # Group by timestamp and component, then calculate average
            if not in_df.empty:
                in_avg = in_df.groupby(['timestamp', 'component']).agg({'value': 'mean'}).reset_index()
                for component in in_avg['component'].unique():
                    component_df = in_avg[in_avg['component'] == component]
                    fig.add_trace(go.Scatter(
                        x=component_df['timestamp'],
                        y=component_df['value'],
                        mode='lines',
                        name=f"{component} In",
                        line=dict(color='#3498db')
                    ))
            
            if not out_df.empty:
                out_avg = out_df.groupby(['timestamp', 'component']).agg({'value': 'mean'}).reset_index()
                for component in out_avg['component'].unique():
                    component_df = out_avg[out_avg['component'] == component]
                    fig.add_trace(go.Scatter(
                        x=component_df['timestamp'],
                        y=component_df['value'],
                        mode='lines',
                        name=f"{component} Out",
                        line=dict(color='#f39c12')
                    ))
                
            # Update layout
            fig.update_layout(
                title="Network Traffic",
                xaxis_title="Time",
                yaxis_title="Traffic (KB/s)",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
                legend=dict(
                    orientation="h",
                    yanchor="bottom",
                    y=1.02,
                    xanchor="right",
                    x=1
                ),
                hovermode="x unified"
            )
            
            return fig
        except Exception as e:
            print(f"Error creating network chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update resource consumers table
    @app.callback(
        Output("resource-consumers-table", "children"),
        [Input("resource-data-json", "data")]
    )
    def update_resource_consumers(resource_data):
        if not resource_data or "top_consumers" not in resource_data:
            return html.P("No resource consumer data available", className="text-muted")
            
        # Extract data
        consumers = resource_data.get("top_consumers", [])
        
        if not consumers:
            return html.P("No resource consumer data available", className="text-muted")
            
        # Create the table
        return dbc.Table(
            [
                html.Thead(
                    html.Tr([
                        html.Th("Component"),
                        html.Th("CPU (%)"),
                        html.Th("Memory (%)"),
                        html.Th("Disk I/O (MB/s)"),
                        html.Th("Network (KB/s)"),
                    ])
                ),
                html.Tbody([
                    html.Tr([
                        html.Td(consumer.get("component", "Unknown")),
                        html.Td(
                            f"{consumer.get('cpu', 0):.1f}%", 
                            className=get_threshold_class(consumer.get('cpu', 0), [60, 80])
                        ),
                        html.Td(
                            f"{consumer.get('memory', 0):.1f}%", 
                            className=get_threshold_class(consumer.get('memory', 0), [70, 90])
                        ),
                        html.Td(
                            f"{consumer.get('disk_io', 0):.2f} MB/s"
                        ),
                        html.Td(
                            f"{consumer.get('network', 0):.2f} KB/s"
                        ),
                    ]) for consumer in consumers
                ])
            ],
            bordered=True,
            hover=True,
            responsive=True,
            striped=True,
            className="table-sm"
        )

    # Callback to update resource alerts
    @app.callback(
        Output("resource-alerts", "children"),
        [Input("resource-data-json", "data")]
    )
    def update_resource_alerts(resource_data):
        if not resource_data or "alerts" not in resource_data:
            return html.P("No resource alerts available", className="text-muted")
            
        # Extract data
        alerts = resource_data.get("alerts", [])
        
        if not alerts:
            return html.P("No resource alerts", className="text-success")
            
        # Create alerts list
        return html.Div([
            html.Div([
                html.Div([
                    html.Div([
                        create_severity_badge(alert.get("severity", "unknown")),
                        html.Span(alert.get("message", "Unknown alert"), className="ms-2"),
                    ], className="d-flex align-items-center mb-1"),
                    html.Small([
                        html.Span(f"Component: {alert.get('component', 'Unknown')} | "),
                        html.Span(f"Time: {format_timestamp(alert.get('timestamp'))}"),
                    ], className="text-muted"),
                ], className="alert-item pb-2 mb-2 border-bottom")
            for alert in alerts
            ])
        ])

    # Callback to update resource predictions
    @app.callback(
        Output("resource-predictions", "children"),
        [Input("resource-data-json", "data")]
    )
    def update_resource_predictions(resource_data):
        if not resource_data or "predictions" not in resource_data:
            return html.P("No resource predictions available", className="text-muted")
            
        # Extract data
        predictions = resource_data.get("predictions", [])
        
        if not predictions:
            return html.P("No resource predictions available", className="text-muted")
            
        # Create predictions list
        return html.Div([
            html.Div([
                html.Div([
                    html.H6(prediction.get("resource", "Unknown"), className="mb-1"),
                    html.P(prediction.get("message", ""), className="mb-1"),
                    html.Small([
                        html.Span(f"Predicted in: {prediction.get('time_to_event', 'Unknown')} | "),
                        html.Span(f"Confidence: {prediction.get('confidence', 0) * 100:.0f}%"),
                    ], className="text-muted"),
                ], className="prediction-item pb-2 mb-2 border-bottom")
            for prediction in predictions
            ])
        ])

    # Callback to handle timeframe buttons for resource charts
    @app.callback(
        [
            Output("resource-timeframe", "data"),
            Output("1h-resource-btn", "active"),
            Output("6h-resource-btn", "active"),
            Output("24h-resource-btn", "active"),
            Output("7d-resource-btn", "active"),
        ],
        [
            Input("1h-resource-btn", "n_clicks"),
            Input("6h-resource-btn", "n_clicks"),
            Input("24h-resource-btn", "n_clicks"),
            Input("7d-resource-btn", "n_clicks"),
        ],
        [State("resource-timeframe", "data")]
    )
    def update_resource_timeframe(btn_1h, btn_6h, btn_24h, btn_7d, current_timeframe):
        ctx = callback_context
        if not ctx.triggered:
            # Default to 24h view
            return "24h", False, False, True, False
            
        button_id = ctx.triggered[0]["prop_id"].split(".")[0]
        timeframes = {
            "1h-resource-btn": "1h",
            "6h-resource-btn": "6h",
            "24h-resource-btn": "24h",
            "7d-resource-btn": "7d"
        }
        
        if button_id in timeframes:
            new_timeframe = timeframes[button_id]
            return (
                new_timeframe,
                new_timeframe == "1h",
                new_timeframe == "6h",
                new_timeframe == "24h",
                new_timeframe == "7d"
            )
        
        # Default case
        return current_timeframe, False, False, True, False

# Helper functions
def create_gauge_figure(value, title, suffix="%", thresholds=[60, 80]):
    """Create a gauge figure with appropriate coloring based on thresholds."""
    # Determine color based on thresholds
    if value >= thresholds[1]:
        color = "red"
    elif value >= thresholds[0]:
        color = "orange"
    else:
        color = "green"
        
    # Create steps for gauge
    steps = [
        {"range": [0, thresholds[0]], "color": "rgba(0, 200, 0, 0.2)"},
        {"range": [thresholds[0], thresholds[1]], "color": "rgba(255, 165, 0, 0.2)"},
        {"range": [thresholds[1], 100], "color": "rgba(255, 0, 0, 0.2)"}
    ]
    
    # Create the gauge
    fig = go.Figure(go.Indicator(
        mode="gauge+number",
        value=value,
        title={"text": title},
        number={"suffix": suffix},
        gauge={
            "axis": {"range": [0, 100], "tickwidth": 1},
            "bar": {"color": color},
            "steps": steps,
            "threshold": {
                "line": {"color": "white", "width": 4},
                "thickness": 0.75,
                "value": thresholds[1]
            }
        }
    ))
    
    # Update layout
    fig.update_layout(
        height=150,
        margin=dict(l=20, r=20, t=30, b=20),
        paper_bgcolor="rgba(0,0,0,0)",
        plot_bgcolor="rgba(0,0,0,0)",
        font={"color": "white"}
    )
    
    return fig

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

def format_network_rate(rate_kbps):
    """Format network rate in appropriate units."""
    if rate_kbps < 1000:
        return f"{rate_kbps:.1f} KB/s"
    elif rate_kbps < 1000000:
        return f"{rate_kbps/1000:.1f} MB/s"
    else:
        return f"{rate_kbps/1000000:.1f} GB/s"

def create_severity_badge(severity):
    """Create a badge for alert severity."""
    severity = severity.lower()
    if severity == "critical":
        return html.Span("Critical", className="badge bg-danger")
    elif severity == "warning":
        return html.Span("Warning", className="badge bg-warning")
    elif severity == "info":
        return html.Span("Info", className="badge bg-info")
    else:
        return html.Span("Unknown", className="badge bg-secondary")

def get_threshold_class(value, thresholds):
    """Get CSS class based on threshold values."""
    if value >= thresholds[1]:
        return "text-danger"
    elif value >= thresholds[0]:
        return "text-warning"
    else:
        return ""

def format_timestamp(timestamp_str):
    """Format timestamp string."""
    if not timestamp_str:
        return "N/A"
        
    try:
        dt = datetime.fromisoformat(timestamp_str.replace("Z", "+00:00"))
        return dt.strftime("%Y-%m-%d %H:%M:%S")
    except:
        return timestamp_str