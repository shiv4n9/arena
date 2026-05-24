import pandas as pd
import matplotlib.pyplot as plt
import matplotlib as mpl

def main():
    # Set eye-catching neon/dark theme
    plt.style.use("dark_background")
    mpl.rcParams["axes.prop_cycle"] = mpl.cycler(color=["#00ffff", "#ff00ff", "#ffff00", "#00ff00"])
    mpl.rcParams["axes.grid"] = True
    mpl.rcParams["grid.color"] = "#333333"
    mpl.rcParams["axes.edgecolor"] = "#555555"
    
    df = pd.read_csv("data/sweep_results.csv")
    
    # Filter where Sigma_A >= Sigma_B to show the penalty
    df_plot = df[df["Sigma_A"] >= df["Sigma_B"]].copy()
    df_plot["Variance_Gap"] = df_plot["Sigma_A"] - df_plot["Sigma_B"]
    
    # Plot 1: Competitive Cost vs Variance Gap
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(df_plot["Variance_Gap"], df_plot["Competitive_Cost"], marker='o', linewidth=2.5, color="#ff00ff")
    ax.set_title("Competitive Cost of Latency Variance", fontsize=16, fontweight='bold', color='white')
    ax.set_xlabel("Variance Gap ($\\sigma_A - \\sigma_B$)", fontsize=14)
    ax.set_ylabel("PnL Penalty (Solo - Agent A)", fontsize=14)
    ax.fill_between(df_plot["Variance_Gap"], df_plot["Competitive_Cost"], color="#ff00ff", alpha=0.1)
    plt.tight_layout()
    plt.savefig("data/competitive_cost_curve.png", dpi=300)
    plt.close()
    
    # Plot 2: Win Rate and Nash Boundary vs Variance Gap
    fig, ax1 = plt.subplots(figsize=(10, 6))
    color1 = "#00ffff"
    ax1.set_xlabel("Variance Gap ($\\sigma_A - \\sigma_B$)", fontsize=14)
    ax1.set_ylabel("Win Rate (%)", color=color1, fontsize=14)
    ax1.plot(df_plot["Variance_Gap"], df_plot["Win_Rate_A"] * 100, marker='s', linewidth=2.5, color=color1, label="Agent A Win Rate")
    ax1.tick_params(axis='y', labelcolor=color1)
    
    ax2 = ax1.twinx()  
    color2 = "#ffff00"
    ax2.set_ylabel("Nash Boundary ($b_A$)", color=color2, fontsize=14)  
    ax2.plot(df_plot["Variance_Gap"], df_plot["Nash_B_A"], marker='^', linewidth=2.5, color=color2, linestyle='--', label="Agent A Nash Boundary")
    ax2.tick_params(axis='y', labelcolor=color2)
    
    plt.title("Win Rate & Boundary Compression under Competitive Pressure", fontsize=16, fontweight='bold', color='white')
    fig.tight_layout()
    plt.savefig("data/nash_boundary_compression.png", dpi=300)
    plt.close()
    
    print("Plots generated successfully in data/ directory.")

if __name__ == "__main__":
    main()
