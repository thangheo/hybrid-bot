"""
Health dashboard component for the crypto trading monitoring system.
Displays system health status, component states, and latency metrics.
"""

import requests
import pandas as pd
from dash import dcc, html, callback_context
import dash_bootstrap_components as dbc
from dash.dependencies import Input, Output, State
import plotly.graph_objs as go
import plotly.express as px
from datetime import datetime, timedelta

# Function to create the layout for the health dashboard
def create_health_layout(api_base_url):
    """Create the layout for the health dashboard component."""
    return html.Div(
        [
            dbc.Row(
                [
                    dbc.Col(html.H2("System Health Status", className="mb-4"), width=8),
                    dbc.Col(
                        dbc.Button(
                            "Refresh Health Data", 
                            id="refresh-health-button", 
                            color="primary", 
                            className="float-end"
                        ),
                        width=4,
                        className="d-flex justify-content-end",
                    ),
                ]
            ),
            
            # Overall system health card
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Overall System Status", className="card-title"),
                                    html.Div(id="overall-health-indicator", className="mt-3 mb-3"),
                                    html.P(id="overall-health-message", className="card-text"),
                                    html.P("Last checked: ", className="text-muted"),
                                    html.P(id="health-last-updated", className="text-muted"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=4,
                    ),
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Current Alerts", className="card-title"),
                                    html.Div(id="health-alerts-summary"),
                                    dbc.Button(
                                        "View Alerts", 
                                        href="/alerts", 
                                        color="link", 
                                        className="mt-3"
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=4,
                    ),
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Response Times", className="card-title"),
                                    dcc.Graph(
                                        id="system-response-time-gauge",
                                        config={"displayModeBar": False},
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=4,
                    ),
                ]
            ),
            
            # Component health status
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Component Health Status", className="card-title"),
                                    html.Div(id="component-health-table"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Latency trends
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Latency Trends", className="card-title"),
                                    dbc.Row(
                                        [
                                            dbc.Col(
                                                dbc.ButtonGroup(
                                                    [
                                                        dbc.Button("1 Hour", id="1h-latency-btn", color="primary", outline=True, n_clicks=0),
                                                        dbc.Button("6 Hours", id="6h-latency-btn", color="primary", outline=True, n_clicks=0),
                                                        dbc.Button("24 Hours", id="24h-latency-btn", color="primary", outline=True, active=True, n_clicks=1),
                                                        dbc.Button("7 Days", id="7d-latency-btn", color="primary", outline=True, n_clicks=0),
                                                    ],
                                                    className="mb-3",
                                                ),
                                                width=12,
                                            ),
                                        ]
                                    ),
                                    dcc.Graph(
                                        id="latency-trends-graph",
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
            
            # Component dependencies
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Component Dependencies", className="card-title"),
                                    dcc.Graph(
                                        id="component-dependency-graph",
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
            
            # Hidden components for storing state
            dcc.Store(id="latency-timeframe", data="24h"),
            dcc.Store(id="health-data-json"),
            dcc.Store(id="components-data-json"),
            dcc.Store(id="latency-data-json"),
            
            # Interval component for auto-updates
            dcc.Interval(
                id="health-interval-component",
                interval=60 * 1000,  # 60 seconds
                n_intervals=0,
            ),
        ]
    )

def register_health_callbacks(app, api_base_url):
    """Register callbacks for the health dashboard component."""
    
    # Callback to fetch health data
    @app.callback(
        [
            Output("health-data-json", "data"),
            Output("components-data-json", "data"),
            Output("latency-data-json", "data"),
            Output("health-last-updated", "children"),
        ],
        [
            Input("health-interval-component", "n_intervals"),
            Input("refresh-health-button", "n_clicks"),
            Input("latency-timeframe", "data"),
        ],
    )
    def fetch_health_data(n_intervals, n_clicks, timeframe):
        # Fetch system health data
        try:
            health_response = requests.get(f"{api_base_url}/api/v1/health/system", timeout=5)
            health_data = health_response.json() if health_response.status_code == 200 else {}

            components_response = requests.get(f"{api_base_url}/api/v1/health/components", timeout=5)
            components_data = components_response.json() if components_response.status_code == 200 else {}

            # Fetch latency data with appropriate timeframe
            latency_response = requests.get(
                f"{api_base_url}/api/v1/health/latency?timeframe={timeframe}", 
                timeout=5
            )
            latency_data = latency_response.json() if latency_response.status_code == 200 else {}

            # Update the timestamp
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            return health_data, components_data, latency_data, timestamp
        except Exception as e:
            print(f"Error fetching health data: {e}")
            # Return empty data and error message
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S") + " (Error fetching data)"
            return {}, {}, {}, timestamp

    # Callback to update the overall system health indicator
    @app.callback(
        [
            Output("overall-health-indicator", "children"),
            Output("overall-health-message", "children"),
            Output("health-alerts-summary", "children"),
        ],
        [Input("health-data-json", "data")]
    )
    def update_health_indicators(health_data):
        if not health_data:
            return html.Div(
                html.I(className="fas fa-question-circle fa-3x text-secondary"),
                className="text-center"
            ), "Unable to fetch health data", "No alert data available"
        
        # Extract status and create appropriate indicator
        status = health_data.get("status", "unknown").lower()
        if status == "healthy":
            icon = html.I(className="fas fa-check-circle fa-3x text-success")
            message = "All systems operational"
        elif status == "degraded":
            icon = html.I(className="fas fa-exclamation-triangle fa-3x text-warning")
            message = "Some systems experiencing issues"
        elif status == "critical":
            icon = html.I(className="fas fa-times-circle fa-3x text-danger")
            message = "Critical system issues detected"
        else:
            icon = html.I(className="fas fa-question-circle fa-3x text-secondary")
            message = "System status unknown"
            
        # Create alerts summary
        alerts = health_data.get("alerts", [])
        if not alerts:
            alerts_summary = html.P("No active alerts", className="text-success")
        else:
            # Count alerts by severity
            critical = sum(1 for alert in alerts if alert.get("severity") == "critical")
            warning = sum(1 for alert in alerts if alert.get("severity") == "warning")
            info = sum(1 for alert in alerts if alert.get("severity") == "info")
            
            alerts_summary = html.Div([
                html.P(f"Active Alerts:", className="mb-2"),
                html.Div([
                    html.Span(f"{critical} Critical", className="badge bg-danger me-2") if critical else None,
                    html.Span(f"{warning} Warning", className="badge bg-warning me-2") if warning else None,
                    html.Span(f"{info} Info", className="badge bg-info") if info else None,
                ])
            ])
            
        return html.Div(icon, className="text-center"), message, alerts_summary

    # Callback to create the response time gauge
    @app.callback(
        Output("system-response-time-gauge", "figure"),
        [Input("health-data-json", "data")]
    )
    def update_response_time_gauge(health_data):
        if not health_data:
            # Return empty gauge
            return create_empty_gauge("No Data")
            
        # Extract average response time
        avg_response_time = health_data.get("average_response_time_ms", 0)
        
        # Create a gauge figure
        fig = go.Figure(go.Indicator(
            mode="gauge+number",
            value=avg_response_time,
            title={"text": "Avg Response Time (ms)"},
            gauge={
                "axis": {"range": [0, 500], "tickwidth": 1},
                "bar": {"color": get_response_time_color(avg_response_time)},
                "steps": [
                    {"range": [0, 100], "color": "rgba(0, 200, 0, 0.2)"},
                    {"range": [100, 200], "color": "rgba(255, 255, 0, 0.2)"},
                    {"range": [200, 500], "color": "rgba(255, 0, 0, 0.2)"}
                ],
                "threshold": {
                    "line": {"color": "red", "width": 4},
                    "thickness": 0.75,
                    "value": 300
                }
            }
        ))
        
        # Update layout for better fit
        fig.update_layout(
            height=200,
            margin=dict(l=20, r=20, t=30, b=20),
            paper_bgcolor="rgba(0,0,0,0)",
            plot_bgcolor="rgba(0,0,0,0)",
            font={"color": "white"}
        )
        
        return fig

    # Callback to generate the component health table
    @app.callback(
        Output("component-health-table", "children"),
        [Input("components-data-json", "data")]
    )
    def update_component_health_table(components_data):
        if not components_data or not components_data.get("components"):
            return html.P("No component data available", className="text-muted")
            
        components = components_data.get("components", [])
        
        # Create the table
        return dbc.Table([
            html.Thead(
                html.Tr([
                    html.Th("Component"),
                    html.Th("Status"),
                    html.Th("Uptime"),
                    html.Th("Last Check"),
                    html.Th("Version"),
                ])
            ),
            html.Tbody([
                html.Tr([
                    html.Td(component.get("name", "Unknown")),
                    html.Td(create_status_badge(component.get("status", "unknown"))),
                    html.Td(format_uptime(component.get("uptime_seconds", 0))),
                    html.Td(format_timestamp(component.get("last_check"))),
                    html.Td(component.get("version", "N/A")),
                ]) for component in components
            ])
        ], bordered=True, hover=True, responsive=True, striped=True, className="table-sm")

    # Callback to update the latency trends graph
    @app.callback(
        Output("latency-trends-graph", "figure"),
        [Input("latency-data-json", "data")]
    )
    def update_latency_trends(latency_data):
        if not latency_data or not latency_data.get("data"):
            # Return empty chart
            return create_empty_chart("No latency data available")
            
        # Parse latency data
        data = latency_data.get("data", [])
        
        # Convert to dataframe for easier processing
        try:
            df = pd.DataFrame(data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add traces for different components
            for component in df['component'].unique():
                component_df = df[df['component'] == component]
                fig.add_trace(go.Scatter(
                    x=component_df['timestamp'],
                    y=component_df['latency_ms'],
                    mode='lines',
                    name=component
                ))
                
            # Update layout
            fig.update_layout(
                title="Component Latency Over Time",
                xaxis_title="Time",
                yaxis_title="Latency (ms)",
                legend_title="Components",
                height=400,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=40, b=40),
            )
            
            return fig
        except Exception as e:
            print(f"Error creating latency chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to update the component dependency graph
    @app.callback(
        Output("component-dependency-graph", "figure"),
        [Input("components-data-json", "data")]
    )
    def update_dependency_graph(components_data):
        if not components_data or not components_data.get("dependencies"):
            return create_empty_chart("No dependency data available")
            
        try:
            dependencies = components_data.get("dependencies", [])
            
            # Create network graph
            nodes = []
            edges = []
            
            # Process components first
            components = components_data.get("components", [])
            for component in components:
                name = component.get("name", "Unknown")
                status = component.get("status", "unknown").lower()
                nodes.append({
                    "name": name,
                    "status": status,
                    "color": get_status_color(status)
                })
            
            # Process dependencies
            for dep in dependencies:
                source = dep.get("source")
                target = dep.get("target")
                edges.append({
                    "source": source,
                    "target": target,
                    "value": 1
                })
            
            # Create node and edge trace lists for the graph
            node_x = []
            node_y = []
            node_text = []
            node_color = []
            
            # Create a simple force-directed layout
            # This is a simplified approach - for a real app you might want to use a proper layout algorithm
            import networkx as nx
            G = nx.Graph()
            
            # Add nodes and edges
            for node in nodes:
                G.add_node(node["name"])
                
            for edge in edges:
                G.add_edge(edge["source"], edge["target"])
                
            # Use NetworkX's spring layout
            pos = nx.spring_layout(G)
            
            for node in nodes:
                name = node["name"]
                x, y = pos[name]
                node_x.append(x)
                node_y.append(y)
                node_text.append(name)
                node_color.append(node["color"])
            
            # Create the node trace
            node_trace = go.Scatter(
                x=node_x, y=node_y,
                mode='markers+text',
                text=node_text,
                hoverinfo='text',
                marker=dict(
                    showscale=False,
                    color=node_color,
                    size=20,
                    line=dict(width=2, color='white')
                ),
                textposition="top center"
            )
            
            # Create the edge traces
            edge_traces = []
            for edge in edges:
                source = edge["source"]
                target = edge["target"]
                
                # Get positions
                x0, y0 = pos[source]
                x1, y1 = pos[target]
                
                # Create line trace
                edge_trace = go.Scatter(
                    x=[x0, x1], 
                    y=[y0, y1],
                    mode='lines',
                    line=dict(width=1, color='#888'),
                    hoverinfo='none'
                )
                edge_traces.append(edge_trace)
            
            # Create figure
            fig = go.Figure(data=edge_traces + [node_trace],
                           layout=go.Layout(
                                showlegend=False,
                                hovermode='closest',
                                margin=dict(l=20, r=20, t=20, b=20),
                                xaxis=dict(showgrid=False, zeroline=False, showticklabels=False),
                                yaxis=dict(showgrid=False, zeroline=False, showticklabels=False),
                                height=400,
                                template="plotly_dark"
                            )
                          )
            
            return fig
        except Exception as e:
            print(f"Error creating dependency graph: {e}")
            return create_empty_chart(f"Error creating dependency graph: {str(e)}")

    # Callback to handle timeframe buttons for latency chart
    @app.callback(
        [
            Output("latency-timeframe", "data"),
            Output("1h-latency-btn", "active"),
            Output("6h-latency-btn", "active"),
            Output("24h-latency-btn", "active"),
            Output("7d-latency-btn", "active"),
        ],
        [
            Input("1h-latency-btn", "n_clicks"),
            Input("6h-latency-btn", "n_clicks"),
            Input("24h-latency-btn", "n_clicks"),
            Input("7d-latency-btn", "n_clicks"),
        ],
        [State("latency-timeframe", "data")]
    )
    def update_timeframe(btn_1h, btn_6h, btn_24h, btn_7d, current_timeframe):
        ctx = callback_context
        if not ctx.triggered:
            # Default to 24h view
            return "24h", False, False, True, False
            
        button_id = ctx.triggered[0]["prop_id"].split(".")[0]
        timeframes = {
            "1h-latency-btn": "1h",
            "6h-latency-btn": "6h",
            "24h-latency-btn": "24h",
            "7d-latency-btn": "7d"
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
def create_empty_gauge(message):
    """Create an empty gauge figure with a message."""
    fig = go.Figure(go.Indicator(
        mode="gauge",
        gauge={"axis": {"range": [0, 100]}},
        title={"text": message}
    ))
    fig.update_layout(
        height=200,
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
        height=400,
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

def get_response_time_color(ms):
    """Get a color based on response time."""
    if ms < 100:
        return "green"
    elif ms < 200:
        return "yellow"
    else:
        return "red"

def create_status_badge(status):
    """Create a badge for component status."""
    status = status.lower()
    if status == "healthy" or status == "ok":
        return html.Span("Healthy", className="badge bg-success")
    elif status == "degraded" or status == "warning":
        return html.Span("Degraded", className="badge bg-warning")
    elif status == "critical" or status == "error":
        return html.Span("Critical", className="badge bg-danger")
    else:
        return html.Span("Unknown", className="badge bg-secondary")

def get_status_color(status):
    """Get color for dependency graph nodes."""
    status = status.lower()
    if status == "healthy" or status == "ok":
        return "green"
    elif status == "degraded" or status == "warning":
        return "yellow"
    elif status == "critical" or status == "error":
        return "red"
    else:
        return "grey"

def format_uptime(seconds):
    """Format uptime seconds into a readable string."""
    if seconds is None:
        return "Unknown"
        
    if seconds < 60:
        return f"{seconds} sec"
    elif seconds < 3600:
        return f"{seconds//60} min {seconds%60} sec"
    elif seconds < 86400:
        return f"{seconds//3600} hrs {(seconds%3600)//60} min"
    else:
        return f"{seconds//86400} days {(seconds%86400)//3600} hrs"

def format_timestamp(timestamp_str):
    """Format timestamp string."""
    if not timestamp_str:
        return "N/A"
        
    try:
        dt = datetime.fromisoformat(timestamp_str.replace("Z", "+00:00"))
        return dt.strftime("%Y-%m-%d %H:%M:%S")
    except:
        return timestamp_str