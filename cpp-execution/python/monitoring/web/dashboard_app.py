"""
Main dashboard application for the crypto trading monitoring system.
Integrates all component dashboards into a single web application.
"""

import os
import dash
from dash import dcc, html
from dash.dependencies import Input, Output
import dash_bootstrap_components as dbc

# Import components
from monitoring.web.components.health_dashboard import create_health_layout, register_health_callbacks
from monitoring.web.components.alerts_dashboard import create_alerts_layout, register_alerts_callbacks
from monitoring.web.components.performance_dashboard import create_performance_layout, register_performance_callbacks
from monitoring.web.components.resource_dashboard import create_resource_layout, register_resource_callbacks

# Initialize the Dash app with a Bootstrap theme
app = dash.Dash(
    __name__,
    external_stylesheets=[dbc.themes.DARKLY],  # Dark theme for monitoring dashboards
    suppress_callback_exceptions=True,
    meta_tags=[
        {"name": "viewport", "content": "width=device-width, initial-scale=1"}
    ],
)

app.title = "Trading System Monitoring"

# API base URL - can be configured via environment variables
API_BASE_URL = os.environ.get("API_BASE_URL", "http://localhost:8000")

# Define the layout of the app
app.layout = dbc.Container(
    [
        dbc.Row(
            dbc.Col(
                html.Div(
                    [
                        html.H1("Trading System Monitoring", className="dashboard-title"),
                        html.P("Real-time monitoring for the crypto trading platform", className="dashboard-subtitle"),
                    ],
                    className="header p-4",
                ),
                width=12,
            ),
        ),
        dbc.Row(
            [
                dbc.Col(
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Navigation", className="card-title"),
                                html.Hr(),
                                dbc.Nav(
                                    [
                                        dbc.NavLink("Health Status", href="/health", active="exact", id="health-link"),
                                        dbc.NavLink("Alerts", href="/alerts", active="exact", id="alerts-link"),
                                        dbc.NavLink("Performance", href="/performance", active="exact", id="performance-link"),
                                        dbc.NavLink("Resources", href="/resources", active="exact", id="resource-link"),
                                    ],
                                    vertical=True,
                                    pills=True,
                                ),
                                html.Hr(),
                                html.Div(
                                    [
                                        html.P("Last Updated:", className="text-muted mb-0"),
                                        html.P(id="update-timestamp", className="text-light"),
                                        dbc.Button(
                                            "Refresh All", 
                                            id="refresh-button", 
                                            color="primary", 
                                            className="w-100"
                                        ),
                                    ]
                                )
                            ]
                        ),
                        className="sidebar h-100",
                    ),
                    width=2,
                    className="sidebar-col",
                ),
                dbc.Col(
                    [
                        dcc.Location(id="url", refresh=False),
                        html.Div(id="page-content", className="content-container"),
                    ],
                    width=10,
                    className="content-col",
                ),
            ],
            className="g-0 main-content",
        ),
        # Store components for holding data
        dcc.Store(id="health-data-store"),
        dcc.Store(id="alerts-data-store"),
        dcc.Store(id="performance-data-store"),
        dcc.Store(id="resource-data-store"),
        # Interval component for automatic updates
        dcc.Interval(
            id="interval-component",
            interval=30 * 1000,  # Update every 30 seconds
            n_intervals=0,
        ),
    ],
    fluid=True,
    className="main-container",
)

# Callback to update the page content based on the URL
@app.callback(
    Output("page-content", "children"),
    [Input("url", "pathname")]
)
def display_page(pathname):
    if pathname == "/health" or pathname == "/":
        return create_health_layout(API_BASE_URL)
    elif pathname == "/alerts":
        return create_alerts_layout(API_BASE_URL)
    elif pathname == "/performance":
        return create_performance_layout(API_BASE_URL)
    elif pathname == "/resources":
        return create_resource_layout(API_BASE_URL)
    else:
        # Default to health dashboard
        return create_health_layout(API_BASE_URL)

# Callback to update active link
@app.callback(
    [
        Output("health-link", "active"),
        Output("alerts-link", "active"),
        Output("performance-link", "active"),
        Output("resource-link", "active"),
    ],
    [Input("url", "pathname")]
)
def update_active_links(pathname):
    if pathname == "/" or pathname == "/health":
        return True, False, False, False
    elif pathname == "/alerts":
        return False, True, False, False
    elif pathname == "/performance":
        return False, False, True, False
    elif pathname == "/resources":
        return False, False, False, True
    # Default case
    return True, False, False, False

# Update timestamp when any refresh occurs
@app.callback(
    Output("update-timestamp", "children"),
    [Input("interval-component", "n_intervals"),
     Input("refresh-button", "n_clicks")]
)
def update_timestamp(n_intervals, n_clicks):
    from datetime import datetime
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")

# Register callbacks for each component
register_health_callbacks(app, API_BASE_URL)
register_alerts_callbacks(app, API_BASE_URL)
register_performance_callbacks(app, API_BASE_URL)
register_resource_callbacks(app, API_BASE_URL)

# Add CSS for components
app.index_string = '''
<!DOCTYPE html>
<html>
    <head>
        {%metas%}
        <title>{%title%}</title>
        {%favicon%}
        {%css%}
        <style>
            .dashboard-title {
                color: #ffffff;
                font-weight: bold;
            }
            .dashboard-subtitle {
                color: #cccccc;
            }
            .main-container {
                min-height: 100vh;
                background-color: #1a1a1a;
            }
            .header {
                background-color: #2c3e50;
                border-bottom: 1px solid #34495e;
            }
            .sidebar {
                background-color: #2c3e50;
                min-height: calc(100vh - 100px);
            }
            .content-container {
                padding: 20px;
            }
            .card {
                margin-bottom: 20px;
            }
            .alert-indicator {
                height: 15px;
                width: 15px;
                border-radius: 50%;
                display: inline-block;
                margin-right: 10px;
            }
            .alert-critical {
                background-color: #e74c3c;
            }
            .alert-warning {
                background-color: #f39c12;
            }
            .alert-info {
                background-color: #3498db;
            }
            .alert-normal {
                background-color: #2ecc71;
            }
        </style>
    </head>
    <body>
        {%app_entry%}
        <footer>
            {%config%}
            {%scripts%}
            {%renderer%}
        </footer>
    </body>
</html>
'''

if __name__ == "__main__":
    # Run the server
    app.run_server(debug=True, host="0.0.0.0", port=8050)