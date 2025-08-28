"""
Alerts dashboard component for the crypto trading monitoring system.
Displays and manages system alerts, incident tracking, and escalation policies.
"""

import requests
import pandas as pd
from dash import dcc, html, dash_table, callback_context
import dash_bootstrap_components as dbc
from dash.dependencies import Input, Output, State
import plotly.graph_objs as go
import plotly.express as px
from datetime import datetime, timedelta

# Function to create the layout for the alerts dashboard
def create_alerts_layout(api_base_url):
    """Create the layout for the alerts dashboard component."""
    return html.Div(
        [
            dbc.Row(
                [
                    dbc.Col(html.H2("System Alerts", className="mb-4"), width=8),
                    dbc.Col(
                        dbc.Button(
                            "Refresh Alerts", 
                            id="refresh-alerts-button", 
                            color="primary", 
                            className="float-end"
                        ),
                        width=4,
                        className="d-flex justify-content-end",
                    ),
                ]
            ),
            
            # Alert summary cards
            dbc.Row(
                [
                    # Critical Alerts
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4(
                                        [
                                            html.Span("Critical Alerts", className="me-2"),
                                            html.Span(id="critical-count", className="badge bg-danger")
                                        ], 
                                        className="d-flex align-items-center"
                                    ),
                                    html.Div(id="critical-alerts-summary"),
                                ]
                            ),
                            className="mb-4 border-danger",
                            color="danger",
                            outline=True
                        ),
                        width=4,
                    ),
                    # Warning Alerts
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4(
                                        [
                                            html.Span("Warning Alerts", className="me-2"),
                                            html.Span(id="warning-count", className="badge bg-warning")
                                        ], 
                                        className="d-flex align-items-center"
                                    ),
                                    html.Div(id="warning-alerts-summary"),
                                ]
                            ),
                            className="mb-4 border-warning",
                            color="warning",
                            outline=True
                        ),
                        width=4,
                    ),
                    # Info Alerts
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4(
                                        [
                                            html.Span("Info Alerts", className="me-2"),
                                            html.Span(id="info-count", className="badge bg-info")
                                        ], 
                                        className="d-flex align-items-center"
                                    ),
                                    html.Div(id="info-alerts-summary"),
                                ]
                            ),
                            className="mb-4 border-info",
                            color="info",
                            outline=True
                        ),
                        width=4,
                    ),
                ]
            ),
            
            # Alert filtering and sorting controls
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Filter Alerts", className="card-title"),
                                    dbc.Row(
                                        [
                                            dbc.Col(
                                                [
                                                    html.Label("Severity"),
                                                    dcc.Dropdown(
                                                        id="alert-severity-filter",
                                                        options=[
                                                            {"label": "All Severities", "value": "all"},
                                                            {"label": "Critical", "value": "critical"},
                                                            {"label": "Warning", "value": "warning"},
                                                            {"label": "Info", "value": "info"}
                                                        ],
                                                        value="all",
                                                        clearable=False,
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                            dbc.Col(
                                                [
                                                    html.Label("Component"),
                                                    dcc.Dropdown(
                                                        id="alert-component-filter",
                                                        options=[{"label": "All Components", "value": "all"}],
                                                        value="all",
                                                        clearable=False,
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                            dbc.Col(
                                                [
                                                    html.Label("Status"),
                                                    dcc.Dropdown(
                                                        id="alert-status-filter",
                                                        options=[
                                                            {"label": "All Statuses", "value": "all"},
                                                            {"label": "Active", "value": "active"},
                                                            {"label": "Acknowledged", "value": "acknowledged"},
                                                            {"label": "Resolved", "value": "resolved"}
                                                        ],
                                                        value="active",
                                                        clearable=False,
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                        ]
                                    ),
                                    dbc.Row(
                                        [
                                            dbc.Col(
                                                [
                                                    html.Label("Time Range"),
                                                    dcc.Dropdown(
                                                        id="alert-time-filter",
                                                        options=[
                                                            {"label": "Last Hour", "value": "1h"},
                                                            {"label": "Last 6 Hours", "value": "6h"},
                                                            {"label": "Last 24 Hours", "value": "24h"},
                                                            {"label": "Last 7 Days", "value": "7d"},
                                                            {"label": "All Time", "value": "all"}
                                                        ],
                                                        value="24h",
                                                        clearable=False,
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                            dbc.Col(
                                                [
                                                    html.Label("Sort By"),
                                                    dcc.Dropdown(
                                                        id="alert-sort-by",
                                                        options=[
                                                            {"label": "Newest First", "value": "newest"},
                                                            {"label": "Oldest First", "value": "oldest"},
                                                            {"label": "Severity (High to Low)", "value": "severity_desc"},
                                                            {"label": "Severity (Low to High)", "value": "severity_asc"}
                                                        ],
                                                        value="newest",
                                                        clearable=False,
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                            dbc.Col(
                                                [
                                                    html.Label("Search"),
                                                    dbc.Input(
                                                        id="alert-search-input",
                                                        type="text",
                                                        placeholder="Search alerts...",
                                                        className="mb-3"
                                                    )
                                                ],
                                                width=4,
                                            ),
                                        ]
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Main alerts table
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Active Alerts", className="card-title"),
                                    html.Div(id="alerts-table-container"),
                                    dbc.Pagination(
                                        id="alerts-pagination",
                                        max_value=5,  # Will be updated by callback
                                        fully_expanded=False,
                                        first_last=True,
                                        previous_next=True,
                                        className="mt-3 d-flex justify-content-center"
                                    ),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=12,
                    ),
                ]
            ),
            
            # Alert detail modal
            dbc.Modal(
                [
                    dbc.ModalHeader(dbc.ModalTitle("Alert Details"), close_button=True),
                    dbc.ModalBody(
                        html.Div(id="alert-detail-content")
                    ),
                    dbc.ModalFooter(
                        [
                            dbc.Button(
                                "Acknowledge",
                                id="alert-acknowledge-btn",
                                className="me-2",
                                color="primary"
                            ),
                            dbc.Button(
                                "Resolve",
                                id="alert-resolve-btn",
                                className="me-2",
                                color="success"
                            ),
                            dbc.Button(
                                "Close",
                                id="alert-close-btn",
                                className="ms-auto",
                                color="secondary"
                            ),
                        ]
                    ),
                ],
                id="alert-detail-modal",
                size="lg",
                is_open=False,
            ),
            
            # Alert trends
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Alert Trends", className="card-title"),
                                    dbc.Row(
                                        [
                                            dbc.Col(
                                                dbc.ButtonGroup(
                                                    [
                                                        dbc.Button("24 Hours", id="24h-alerts-btn", color="primary", outline=True, active=True, n_clicks=1),
                                                        dbc.Button("7 Days", id="7d-alerts-btn", color="primary", outline=True, n_clicks=0),
                                                        dbc.Button("30 Days", id="30d-alerts-btn", color="primary", outline=True, n_clicks=0),
                                                    ],
                                                    className="mb-3",
                                                ),
                                                width=12,
                                            ),
                                        ]
                                    ),
                                    dcc.Graph(
                                        id="alert-trends-graph",
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
            
            # Escalation policy and on-call info
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H4("Current On-Call", className="card-title"),
                                    html.Div(id="on-call-info"),
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
                                    html.H4("Active Incidents", className="card-title"),
                                    html.Div(id="active-incidents-info"),
                                ]
                            ),
                            className="mb-4",
                        ),
                        width=6,
                    ),
                ]
            ),
            
            # Hidden components for storing state
            dcc.Store(id="alerts-data-json"),
            dcc.Store(id="incidents-data-json"),
            dcc.Store(id="oncall-data-json"),
            dcc.Store(id="alerts-trend-data-json"),
            dcc.Store(id="alerts-trend-timeframe", data="24h"),
            dcc.Store(id="alert-detail-id"),
            dcc.Store(id="current-page", data=1),
            
            # Interval component for auto-updates
            dcc.Interval(
                id="alerts-interval-component",
                interval=30 * 1000,  # 30 seconds
                n_intervals=0,
            ),
        ]
    )

def register_alerts_callbacks(app, api_base_url):
    """Register callbacks for the alerts dashboard component."""
    
    # Callback to fetch alert data
    @app.callback(
        [
            Output("alerts-data-json", "data"),
            Output("incidents-data-json", "data"),
            Output("oncall-data-json", "data"),
            Output("alerts-trend-data-json", "data"),
        ],
        [
            Input("alerts-interval-component", "n_intervals"),
            Input("refresh-alerts-button", "n_clicks"),
            Input("alert-severity-filter", "value"),
            Input("alert-component-filter", "value"),
            Input("alert-status-filter", "value"),
            Input("alert-time-filter", "value"),
            Input("alert-sort-by", "value"),
            Input("alert-search-input", "value"),
            Input("alerts-trend-timeframe", "data"),
        ],
    )
    def fetch_alerts_data(n_intervals, n_clicks, severity, component, status, time_range,
                          sort_by, search, trend_timeframe):
        # Build query parameters for filtering
        params = {}
        if severity != "all":
            params["severity"] = severity
        if component != "all":
            params["component"] = component
        if status != "all":
            params["status"] = status
        if time_range != "all":
            params["time_range"] = time_range
        if sort_by:
            params["sort_by"] = sort_by
        if search:
            params["search"] = search
            
        try:
            # Fetch alerts data
            alerts_response = requests.get(
                f"{api_base_url}/api/v1/alerts/",
                params=params,
                timeout=5
            )
            alerts_data = alerts_response.json() if alerts_response.status_code == 200 else {"alerts": []}
            
            # Fetch incidents data
            incidents_response = requests.get(f"{api_base_url}/api/v1/alerts/incidents", timeout=5)
            incidents_data = incidents_response.json() if incidents_response.status_code == 200 else {"incidents": []}
            
            # Fetch on-call data
            oncall_response = requests.get(f"{api_base_url}/api/v1/alerts/oncall", timeout=5)
            oncall_data = oncall_response.json() if oncall_response.status_code == 200 else {"oncall": {}}
            
            # Fetch trend data
            trend_response = requests.get(
                f"{api_base_url}/api/v1/alerts/trends?timeframe={trend_timeframe}", 
                timeout=5
            )
            trend_data = trend_response.json() if trend_response.status_code == 200 else {"trend_data": []}
            
            return alerts_data, incidents_data, oncall_data, trend_data
            
        except Exception as e:
            print(f"Error fetching alerts data: {e}")
            # Return empty data in case of error
            return {"alerts": []}, {"incidents": []}, {"oncall": {}}, {"trend_data": []}

    # Callback to update component filter dropdown based on available components
    @app.callback(
        Output("alert-component-filter", "options"),
        [Input("alerts-data-json", "data")]
    )
    def update_component_filter(alerts_data):
        if not alerts_data or "alerts" not in alerts_data:
            return [{"label": "All Components", "value": "all"}]
            
        # Extract unique components from alerts
        components = set()
        for alert in alerts_data.get("alerts", []):
            component = alert.get("component")
            if component:
                components.add(component)
                
        # Create options list
        options = [{"label": "All Components", "value": "all"}]
        options.extend([{"label": comp, "value": comp} for comp in sorted(components)])
        
        return options

    # Callback to update alert count summaries
    @app.callback(
        [
            Output("critical-count", "children"),
            Output("warning-count", "children"),
            Output("info-count", "children"),
            Output("critical-alerts-summary", "children"),
            Output("warning-alerts-summary", "children"),
            Output("info-alerts-summary", "children"),
        ],
        [Input("alerts-data-json", "data")]
    )
    def update_alert_summaries(alerts_data):
        if not alerts_data or "alerts" not in alerts_data:
            return "0", "0", "0", "No critical alerts", "No warning alerts", "No info alerts"
            
        alerts = alerts_data.get("alerts", [])
        
        # Count alerts by severity
        critical = [a for a in alerts if a.get("severity") == "critical" and a.get("status") != "resolved"]
        warning = [a for a in alerts if a.get("severity") == "warning" and a.get("status") != "resolved"]
        info = [a for a in alerts if a.get("severity") == "info" and a.get("status") != "resolved"]
        
        # Create summaries
        critical_summary = create_alert_summary(critical)
        warning_summary = create_alert_summary(warning)
        info_summary = create_alert_summary(info)
        
        return (
            str(len(critical)),
            str(len(warning)),
            str(len(info)),
            critical_summary,
            warning_summary,
            info_summary
        )

    # Callback to create and update the alerts table
    @app.callback(
        [
            Output("alerts-table-container", "children"),
            Output("alerts-pagination", "max_value"),
            Output("alerts-pagination", "active_page")
        ],
        [
            Input("alerts-data-json", "data"),
            Input("current-page", "data")
        ]
    )
    def update_alerts_table(alerts_data, current_page):
        if not alerts_data or "alerts" not in alerts_data:
            return html.P("No alerts data available", className="text-muted"), 1, 1
            
        alerts = alerts_data.get("alerts", [])
        
        if not alerts:
            return html.P("No alerts match the current filters", className="text-muted"), 1, 1
            
        # Create the DataFrame for the table
        df = pd.DataFrame(alerts)
        
        # Pagination
        page_size = 10
        num_pages = (len(df) + page_size - 1) // page_size
        if current_page > num_pages:
            current_page = 1
            
        start_idx = (current_page - 1) * page_size
        end_idx = min(start_idx + page_size, len(df))
        
        # Get current page data
        page_data = df.iloc[start_idx:end_idx]
        
        # Create the table
        table = dash_table.DataTable(
            id="alerts-table",
            columns=[
                {"name": "ID", "id": "id"},
                {"name": "Severity", "id": "severity"},
                {"name": "Component", "id": "component"},
                {"name": "Message", "id": "message"},
                {"name": "Status", "id": "status"},
                {"name": "Created", "id": "created_at"},
            ],
            data=page_data.to_dict("records"),
            row_selectable=False,
            sort_action="none",  # We handle sorting in the backend
            style_cell={
                'textAlign': 'left',
                'backgroundColor': '#2c3e50',
                'color': 'white',
                'padding': '10px',
                'overflow': 'hidden',
                'textOverflow': 'ellipsis',
                'maxWidth': 0,
            },
            style_header={
                'backgroundColor': '#1e2b3c',
                'fontWeight': 'bold',
                'textAlign': 'left',
                'padding': '10px'
            },
            style_data_conditional=[
                {
                    'if': {'filter_query': '{severity} = critical'},
                    'backgroundColor': 'rgba(231, 76, 60, 0.2)',
                    'border-left': '4px solid #e74c3c',
                },
                {
                    'if': {'filter_query': '{severity} = warning'},
                    'backgroundColor': 'rgba(243, 156, 18, 0.2)',
                    'border-left': '4px solid #f39c12',
                },
                {
                    'if': {'filter_query': '{severity} = info'},
                    'backgroundColor': 'rgba(52, 152, 219, 0.2)',
                    'border-left': '4px solid #3498db',
                },
                {
                    'if': {'filter_query': '{status} = resolved'},
                    'color': '#95a5a6',
                },
            ],
            style_table={
                'overflowX': 'auto',
            },
            css=[{
                'selector': '.dash-table-tooltip',
                'rule': 'background-color: #2c3e50; color: white; padding: 10px;'
            }],
            page_current=0,
            page_size=page_size,
            page_action='none',  # We handle pagination ourselves
            tooltip_data=[
                {
                    column: {'value': str(value), 'type': 'text'}
                    for column, value in row.items()
                    if column == 'message'
                } for row in page_data.to_dict('records')
            ],
            tooltip_duration=None,
        )
        
        return table, num_pages, current_page

    # Callback to handle alert table row clicks for details
    @app.callback(
        [
            Output("alert-detail-modal", "is_open"),
            Output("alert-detail-content", "children"),
            Output("alert-detail-id", "data"),
        ],
        [
            Input("alerts-table", "active_cell"),
            Input("alert-close-btn", "n_clicks"),
        ],
        [
            State("alerts-table", "data"),
            State("alert-detail-modal", "is_open"),
        ],
    )
    def show_alert_details(active_cell, close_clicks, table_data, is_open):
        ctx = callback_context
        if not ctx.triggered:
            return False, None, None
            
        trigger_id = ctx.triggered[0]["prop_id"].split(".")[0]
        
        if trigger_id == "alert-close-btn":
            return False, None, None
            
        if trigger_id == "alerts-table" and active_cell:
            row = active_cell["row"]
            alert = table_data[row]
            alert_id = alert.get("id")
            
            # Create detail view
            detail_content = html.Div(
                [
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Alert ID"),
                                    html.P(alert.get("id", "N/A")),
                                ],
                                width=6,
                            ),
                            dbc.Col(
                                [
                                    html.H5("Severity"),
                                    html.Div([
                                        create_severity_badge(alert.get("severity", "unknown"))
                                    ]),
                                ],
                                width=6,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Component"),
                                    html.P(alert.get("component", "N/A")),
                                ],
                                width=6,
                            ),
                            dbc.Col(
                                [
                                    html.H5("Status"),
                                    html.Div([
                                        create_status_badge(alert.get("status", "unknown"))
                                    ]),
                                ],
                                width=6,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Created At"),
                                    html.P(format_timestamp(alert.get("created_at"))),
                                ],
                                width=6,
                            ),
                            dbc.Col(
                                [
                                    html.H5("Last Updated"),
                                    html.P(format_timestamp(alert.get("updated_at"))),
                                ],
                                width=6,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Message"),
                                    html.P(alert.get("message", "N/A")),
                                ],
                                width=12,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Description"),
                                    html.P(alert.get("description", "No detailed description available.")),
                                ],
                                width=12,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Tags"),
                                    html.Div([
                                        html.Span(tag, className="badge bg-secondary me-1")
                                        for tag in alert.get("tags", [])
                                    ] if alert.get("tags") else "No tags"),
                                ],
                                width=12,
                            ),
                        ],
                        className="mb-3",
                    ),
                    dbc.Row(
                        [
                            dbc.Col(
                                [
                                    html.H5("Assigned To"),
                                    html.P(alert.get("assigned_to", "Unassigned")),
                                ],
                                width=12,
                            ),
                        ],
                        className="mb-3",
                    ),
                ]
            )
            
            return True, detail_content, alert_id
            
        return is_open, None, None

    # Callback to handle alert acknowledge and resolve actions
    @app.callback(
        Output("refresh-alerts-button", "n_clicks"),
        [
            Input("alert-acknowledge-btn", "n_clicks"),
            Input("alert-resolve-btn", "n_clicks"),
        ],
        [
            State("alert-detail-id", "data"),
            State("refresh-alerts-button", "n_clicks"),
        ],
    )
    def handle_alert_actions(ack_clicks, resolve_clicks, alert_id, refresh_clicks):
        ctx = callback_context
        if not ctx.triggered or not alert_id:
            return refresh_clicks or 0
            
        trigger_id = ctx.triggered[0]["prop_id"].split(".")[0]
        
        try:
            if trigger_id == "alert-acknowledge-btn" and ack_clicks:
                # Acknowledge the alert
                requests.post(
                    f"{api_base_url}/api/v1/alerts/{alert_id}/acknowledge",
                    timeout=5
                )
                
            elif trigger_id == "alert-resolve-btn" and resolve_clicks:
                # Resolve the alert
                requests.post(
                    f"{api_base_url}/api/v1/alerts/{alert_id}/resolve",
                    timeout=5
                )
                
            # Trigger a refresh
            return (refresh_clicks or 0) + 1
            
        except Exception as e:
            print(f"Error performing alert action: {e}")
            return refresh_clicks or 0

    # Callback to update the alerts trend graph
    @app.callback(
        Output("alert-trends-graph", "figure"),
        [Input("alerts-trend-data-json", "data")]
    )
    def update_alert_trends(trend_data):
        if not trend_data or "trend_data" not in trend_data:
            return create_empty_chart("No trend data available")
            
        data = trend_data.get("trend_data", [])
        
        if not data:
            return create_empty_chart("No trend data available")
            
        try:
            # Convert to dataframe for easier processing
            df = pd.DataFrame(data)
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            
            # Create figure
            fig = go.Figure()
            
            # Add traces for different severity levels
            severity_colors = {
                "critical": "#e74c3c",
                "warning": "#f39c12",
                "info": "#3498db"
            }
            
            for severity in ["critical", "warning", "info"]:
                if severity in df['severity'].values:
                    severity_df = df[df['severity'] == severity]
                    fig.add_trace(go.Scatter(
                        x=severity_df['timestamp'],
                        y=severity_df['count'],
                        mode='lines',
                        name=severity.capitalize(),
                        line=dict(color=severity_colors.get(severity, "#7f8c8d"))
                    ))
                    
            # Update layout
            fig.update_layout(
                title="Alert Frequency Over Time",
                xaxis_title="Time",
                yaxis_title="Number of Alerts",
                legend_title="Severity",
                height=350,
                template="plotly_dark",
                margin=dict(l=40, r=40, t=60, b=40),
            )
            
            return fig
        except Exception as e:
            print(f"Error creating alert trends chart: {e}")
            return create_empty_chart(f"Error creating chart: {str(e)}")

    # Callback to handle timeframe buttons for alert trends chart
    @app.callback(
        [
            Output("alerts-trend-timeframe", "data"),
            Output("24h-alerts-btn", "active"),
            Output("7d-alerts-btn", "active"),
            Output("30d-alerts-btn", "active"),
        ],
        [
            Input("24h-alerts-btn", "n_clicks"),
            Input("7d-alerts-btn", "n_clicks"),
            Input("30d-alerts-btn", "n_clicks"),
        ],
        [State("alerts-trend-timeframe", "data")]
    )
    def update_trend_timeframe(btn_24h, btn_7d, btn_30d, current_timeframe):
        ctx = callback_context
        if not ctx.triggered:
            # Default to 24h view
            return "24h", True, False, False
            
        button_id = ctx.triggered[0]["prop_id"].split(".")[0]
        timeframes = {
            "24h-alerts-btn": "24h",
            "7d-alerts-btn": "7d",
            "30d-alerts-btn": "30d"
        }
        
        if button_id in timeframes:
            new_timeframe = timeframes[button_id]
            return (
                new_timeframe,
                new_timeframe == "24h",
                new_timeframe == "7d",
                new_timeframe == "30d"
            )
        
        # Default case
        return current_timeframe, current_timeframe == "24h", current_timeframe == "7d", current_timeframe == "30d"

    # Callback to update pagination
    @app.callback(
        Output("current-page", "data"),
        [Input("alerts-pagination", "active_page")]
    )
    def update_current_page(active_page):
        return active_page if active_page else 1

    # Callback to update on-call info
    @app.callback(
        Output("on-call-info", "children"),
        [Input("oncall-data-json", "data")]
    )
    def update_oncall_info(oncall_data):
        if not oncall_data or "oncall" not in oncall_data:
            return html.P("No on-call information available", className="text-muted")
            
        oncall = oncall_data.get("oncall", {})
        
        if not oncall:
            return html.P("No on-call information available", className="text-muted")
            
        # Create on-call display
        primary = oncall.get("primary", {})
        secondary = oncall.get("secondary", {})
        
        return html.Div([
            dbc.Row([
                dbc.Col([
                    html.H5("Primary On-Call"),
                    html.P(primary.get("name", "Not assigned"), className="mb-1"),
                    html.P(primary.get("contact", ""), className="text-muted mb-1"),
                    html.P(f"Until: {format_timestamp(primary.get('until'))}", className="small text-muted"),
                ], width=6),
                dbc.Col([
                    html.H5("Secondary On-Call"),
                    html.P(secondary.get("name", "Not assigned"), className="mb-1"),
                    html.P(secondary.get("contact", ""), className="text-muted mb-1"),
                    html.P(f"Until: {format_timestamp(secondary.get('until'))}", className="small text-muted"),
                ], width=6),
            ]),
            html.Hr(),
            html.Div([
                html.H6("Escalation Policy", className="mb-2"),
                html.P(oncall.get("escalation_policy", "Default escalation policy")),
            ]),
        ])

    # Callback to update active incidents info
    @app.callback(
        Output("active-incidents-info", "children"),
        [Input("incidents-data-json", "data")]
    )
    def update_incidents_info(incidents_data):
        if not incidents_data or "incidents" not in incidents_data:
            return html.P("No active incidents", className="text-muted")
            
        incidents = incidents_data.get("incidents", [])
        
        if not incidents:
            return html.P("No active incidents", className="text-muted")
            
        # Create incidents list
        return html.Div([
            html.Div([
                html.Div([
                    html.H6([
                        create_severity_badge(incident.get("severity", "unknown")),
                        html.Span(incident.get("title", "Untitled incident"), className="ms-2"),
                    ], className="mb-1"),
                    html.P([
                        html.Span(f"ID: {incident.get('id', 'N/A')} | "),
                        html.Span(f"Started: {format_timestamp(incident.get('created_at'))} | "),
                        html.Span(f"Assigned: {incident.get('assigned_to', 'Unassigned')}"),
                    ], className="small text-muted"),
                ], className="incident-item pb-2 mb-2 border-bottom")
            for incident in incidents[:5]  # Show up to 5 incidents
            ]),
            html.Div([
                dbc.Button(
                    f"View All ({len(incidents)}) Incidents", 
                    color="link", 
                    size="sm", 
                    className="mt-2"
                ) if len(incidents) > 5 else None
            ])
        ])

# Helper functions
def create_alert_summary(alerts):
    """Create a summary of alerts by component."""
    if not alerts:
        return html.P("No alerts", className="text-muted")
        
    # Group by component
    component_counts = {}
    for alert in alerts:
        component = alert.get("component", "Unknown")
        component_counts[component] = component_counts.get(component, 0) + 1
        
    # Create summary items
    items = []
    for component, count in sorted(component_counts.items(), key=lambda x: x[1], reverse=True):
        items.append(html.Div(
            f"{component}: {count}",
            className="mb-1"
        ))
        
    return html.Div(items[:5])  # Show top 5 components

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

def create_status_badge(status):
    """Create a badge for alert status."""
    status = status.lower()
    if status == "active":
        return html.Span("Active", className="badge bg-danger")
    elif status == "acknowledged":
        return html.Span("Acknowledged", className="badge bg-warning")
    elif status == "resolved":
        return html.Span("Resolved", className="badge bg-success")
    else:
        return html.Span("Unknown", className="badge bg-secondary")

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

def format_timestamp(timestamp_str):
    """Format timestamp string."""
    if not timestamp_str:
        return "N/A"
        
    try:
        dt = datetime.fromisoformat(timestamp_str.replace("Z", "+00:00"))
        return dt.strftime("%Y-%m-%d %H:%M:%S")
    except:
        return timestamp_str