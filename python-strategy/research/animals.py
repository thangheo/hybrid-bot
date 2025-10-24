"""
Mô phỏng Hệ sinh thái khép kín - Chu trình sinh trụ hoại diệt
=============================================================
Chương trình mô phỏng sự phát triển và suy tàn của nhiều loài
trong một hệ sinh thái khép kín theo thời gian.

Author: Claude
Date: 2024
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.animation import FuncAnimation
import seaborn as sns
from dataclasses import dataclass
from typing import List, Tuple
from enum import Enum
import warnings
warnings.filterwarnings('ignore')

# Cấu hình style cho matplotlib
plt.style.use('seaborn-v0_8-darkgrid')
sns.set_palette("husl")

class EvolutionState(Enum):
    """Định nghĩa 4 trạng thái tiến hóa của loài"""
    INIT = 0      # Khởi tạo - dân số nhỏ, có thể tăng nhanh
    GROWTH = 1    # Phát triển - dân số tăng mạnh theo logistic
    DECAY = 2     # Suy tàn - tài nguyên cạn kiệt, dân số giảm
    EXTINCT = 3   # Biến mất - dân số về 0

@dataclass
class Species:
    """
    Lớp đại diện cho một loài trong hệ sinh thái
    """
    id: int
    name: str
    population: float          # Dân số hiện tại
    birth_rate: float          # Tỷ lệ sinh
    death_rate: float          # Tỷ lệ tử
    carrying_capacity: float   # Sức chứa môi trường (K)
    state: EvolutionState      # Trạng thái tiến hóa hiện tại
    state_duration: int        # Thời gian ở trạng thái hiện tại
    population_history: List[float]  # Lịch sử dân số
    state_history: List[int]        # Lịch sử trạng thái
    
    def __post_init__(self):
        """Khởi tạo các thuộc tính lịch sử"""
        self.population_history = [self.population]
        self.state_history = [self.state.value]
        self.state_duration = 0
        
class EcosystemSimulation:
    """
    Lớp chính để mô phỏng hệ sinh thái khép kín
    """
    
    def __init__(self, n_species=30, time_steps=500, seed=42):
        """
        Khởi tạo hệ sinh thái
        
        Parameters:
        -----------
        n_species : int
            Số lượng loài trong hệ sinh thái
        time_steps : int
            Số bước thời gian mô phỏng
        seed : int
            Seed cho random number generator
        """
        np.random.seed(seed)
        self.n_species = n_species
        self.time_steps = time_steps
        self.current_time = 0
        
        # Tham số mô phỏng
        self.noise_level = 0.05  # Mức độ nhiễu ngẫu nhiên
        
        # Ngưỡng chuyển trạng thái
        self.thresholds = {
            'init_to_growth': 0.2,    # N > 20% K: Init → Growth
            'growth_to_decay': 0.8,   # N > 80% K: Growth → Decay
            'decay_to_extinct': 0.05, # N < 5% K: Decay → Extinct
            'min_population': 1.0      # Ngưỡng tối thiểu để tồn tại
        }
        
        # Khởi tạo các loài
        self.species_list = self._initialize_species()
        
    def _initialize_species(self) -> List[Species]:
        """
        Khởi tạo danh sách các loài với tham số ngẫu nhiên
        """
        species_list = []
        
        for i in range(self.n_species):
            # Tạo tham số ngẫu nhiên theo phân phối lognormal
            initial_pop = np.random.lognormal(3, 1) * 10  # 10-500 cá thể
            carrying_capacity = np.random.lognormal(5, 0.5) * 100  # 500-20000 cá thể
            
            # Tỷ lệ sinh/tử ngẫu nhiên
            birth_rate = np.random.uniform(0.1, 0.5)
            death_rate = np.random.uniform(0.05, 0.3)
            
            # Xác định trạng thái ban đầu dựa trên dân số
            if initial_pop < 0.2 * carrying_capacity:
                initial_state = EvolutionState.INIT
            else:
                initial_state = EvolutionState.GROWTH
                
            species = Species(
                id=i,
                name=f"Loài_{i+1:02d}",
                population=initial_pop,
                birth_rate=birth_rate,
                death_rate=death_rate,
                carrying_capacity=carrying_capacity,
                state=initial_state,
                state_duration=0,
                population_history=[],
                state_history=[]
            )
            
            species_list.append(species)
            
        return species_list
    
    def _calculate_growth_rate(self, species: Species) -> float:
        """
        Tính tốc độ tăng trưởng dựa trên trạng thái hiện tại
        
        Parameters:
        -----------
        species : Species
            Loài cần tính toán
            
        Returns:
        --------
        float : Tốc độ tăng trưởng r
        """
        base_r = species.birth_rate - species.death_rate
        
        # Điều chỉnh r theo trạng thái
        if species.state == EvolutionState.INIT:
            # Giai đoạn khởi tạo: tăng trưởng chậm nhưng ổn định
            return base_r * 0.8
            
        elif species.state == EvolutionState.GROWTH:
            # Giai đoạn phát triển: tăng trưởng mạnh
            return base_r * 1.5
            
        elif species.state == EvolutionState.DECAY:
            # Giai đoạn suy tàn: tăng trưởng âm
            return -abs(base_r) * 0.5
            
        else:  # EXTINCT
            return 0
    
    def _update_state(self, species: Species):
        """
        Cập nhật trạng thái của loài dựa trên ngưỡng dân số
        
        Parameters:
        -----------
        species : Species
            Loài cần cập nhật trạng thái
        """
        if species.state == EvolutionState.EXTINCT:
            return
            
        pop_ratio = species.population / species.carrying_capacity
        species.state_duration += 1
        
        # Logic chuyển trạng thái
        if species.state == EvolutionState.INIT:
            if pop_ratio > self.thresholds['init_to_growth']:
                species.state = EvolutionState.GROWTH
                species.state_duration = 0
                
        elif species.state == EvolutionState.GROWTH:
            if pop_ratio > self.thresholds['growth_to_decay']:
                species.state = EvolutionState.DECAY
                species.state_duration = 0
            # Có thể quay lại Init nếu dân số giảm mạnh
            elif pop_ratio < self.thresholds['init_to_growth'] * 0.5:
                species.state = EvolutionState.INIT
                species.state_duration = 0
                
        elif species.state == EvolutionState.DECAY:
            if pop_ratio < self.thresholds['decay_to_extinct']:
                species.state = EvolutionState.EXTINCT
                species.state_duration = 0
            # Có khả năng phục hồi nếu điều kiện thuận lợi
            elif pop_ratio < 0.4 and species.state_duration > 20:
                # Cơ hội phục hồi nhỏ
                if np.random.random() < 0.1:
                    species.state = EvolutionState.INIT
                    species.state_duration = 0
        
        # Kiểm tra ngưỡng tuyệt chủng
        if species.population < self.thresholds['min_population']:
            species.state = EvolutionState.EXTINCT
            species.population = 0
    
    def _update_population(self, species: Species):
        """
        Cập nhật dân số theo phương trình logistic có nhiễu
        
        N_{t+1} = N_t + r * N_t * (1 - N_t/K) + noise
        
        Parameters:
        -----------
        species : Species
            Loài cần cập nhật dân số
        """
        if species.state == EvolutionState.EXTINCT:
            species.population = 0
            return
            
        r = self._calculate_growth_rate(species)
        K = species.carrying_capacity
        N = species.population
        
        # Phương trình logistic
        logistic_term = r * N * (1 - N/K)
        
        # Thêm nhiễu ngẫu nhiên
        noise = np.random.normal(0, self.noise_level * N)
        
        # Thêm yếu tố mùa (dao động tuần hoàn)
        seasonal_factor = 0.1 * np.sin(2 * np.pi * self.current_time / 50)
        
        # Cập nhật dân số
        species.population = max(0, N + logistic_term + noise + seasonal_factor * N)
        
        # Giới hạn dân số không vượt quá 1.5 * K
        species.population = min(species.population, 1.5 * K)
    
    def simulate_step(self):
        """
        Thực hiện một bước mô phỏng cho tất cả các loài
        """
        for species in self.species_list:
            # Cập nhật dân số
            self._update_population(species)
            
            # Cập nhật trạng thái
            self._update_state(species)
            
            # Lưu lịch sử
            species.population_history.append(species.population)
            species.state_history.append(species.state.value)
        
        self.current_time += 1
    
    def run_simulation(self):
        """
        Chạy toàn bộ mô phỏng
        """
        print(f"🌍 Bắt đầu mô phỏng hệ sinh thái với {self.n_species} loài...")
        print(f"⏱️  Thời gian mô phỏng: {self.time_steps} bước")
        print("-" * 50)
        
        for t in range(self.time_steps):
            self.simulate_step()
            
            # In tiến trình mỗi 50 bước
            if (t + 1) % 50 == 0:
                alive_species = sum(1 for s in self.species_list 
                                  if s.state != EvolutionState.EXTINCT)
                print(f"Bước {t+1:3d}: {alive_species} loài còn sống")
        
        print("-" * 50)
        print("✅ Mô phỏng hoàn tất!")
        self._print_summary()
    
    def _print_summary(self):
        """
        In tóm tắt kết quả mô phỏng
        """
        print("\n📊 TÓM TẮT KẾT QUẢ:")
        print("-" * 50)
        
        # Thống kê theo trạng thái cuối cùng
        state_counts = {state: 0 for state in EvolutionState}
        for species in self.species_list:
            state_counts[species.state] += 1
        
        print("Phân bố trạng thái cuối cùng:")
        for state, count in state_counts.items():
            percentage = (count / self.n_species) * 100
            print(f"  {state.name:8s}: {count:2d} loài ({percentage:5.1f}%)")
        
        # Loài phát triển mạnh nhất
        max_pop_species = max(self.species_list, 
                             key=lambda s: max(s.population_history))
        max_population = max(max_pop_species.population_history)
        print(f"\n🏆 Loài phát triển mạnh nhất: {max_pop_species.name}")
        print(f"   Dân số đỉnh điểm: {max_population:,.0f} cá thể")
        
        # Thời gian sống trung bình
        survival_times = []
        for species in self.species_list:
            if species.state == EvolutionState.EXTINCT:
                extinct_time = species.state_history.index(3)
                survival_times.append(extinct_time)
        
        if survival_times:
            avg_survival = np.mean(survival_times)
            print(f"\n⏳ Thời gian sống trung bình (loài tuyệt chủng): {avg_survival:.1f} bước")

    def print_non_extinct_species_features(self, include_history_stats: bool = True):
        """
        In ra các đặc trưng (dựa trên dữ liệu mô phỏng) của những loài không bị tuyệt chủng
        (tức có trạng thái khác EvolutionState.EXTINCT) — không suy diễn.

        Parameters:
        -----------
        include_history_stats : bool
            Nếu True thì in thêm một số thống kê cơ bản từ population_history
        """
        non_extinct = [s for s in self.species_list if s.state != EvolutionState.EXTINCT]
        if not non_extinct:
            print("\nKhông có loài sống sót (tất cả bị tuyệt chủng).")
            return

        print("\n🔎 Danh sách các loài KHÔNG BỊ TUYỆT CHỦNG (theo dữ liệu mô phỏng):")
        print("-" * 50)
        for s in non_extinct:
            final_pop = s.population
            max_pop = max(s.population_history) if s.population_history else final_pop
            mean_pop = np.mean(s.population_history) if s.population_history else final_pop

            print(f"{s.name} (id={s.id}):")
            print(f"  Trạng thái cuối: {s.state.name}, Dân số cuối: {final_pop:.2f}")
            print(f"  birth_rate={s.birth_rate:.3f}, death_rate={s.death_rate:.3f}, carrying_capacity={s.carrying_capacity:.1f}")
            if include_history_stats:
                print(f"  Đỉnh dân số trong mô phỏng: {max_pop:.2f}, Trung bình: {mean_pop:.2f}")
            print("-" * 50)
    
    def plot_results(self, selected_species=None):
        """
        Vẽ biểu đồ kết quả mô phỏng
        
        Parameters:
        -----------
        selected_species : List[int], optional
            Danh sách ID các loài muốn hiển thị. Nếu None, hiển thị tất cả.
        """
        # Tạo figure với nhiều subplot
        fig = plt.figure(figsize=(18, 12))
        fig.suptitle('🌍 MÔ PHỎNG HỆ SINH THÁI KHÉP KÍN - CHU TRÌNH SINH TRỤ HOẠI DIỆT', 
                    fontsize=16, fontweight='bold')
        
        # 1. Biểu đồ tiến hóa dân số
        ax1 = plt.subplot(2, 2, 1)
        self._plot_population_evolution(ax1, selected_species)
        
        # 2. Heatmap trạng thái theo thời gian
        ax2 = plt.subplot(2, 2, 2)
        self._plot_state_heatmap(ax2)
        
        # 3. Biểu đồ phân bố trạng thái theo thời gian
        ax3 = plt.subplot(2, 2, 3)
        self._plot_state_distribution(ax3)
        
        # 4. Biểu đồ tổng dân số và đa dạng sinh học
        ax4 = plt.subplot(2, 2, 4)
        self._plot_ecosystem_metrics_en(ax4)
        
        plt.tight_layout()
        plt.show()
    
    def _plot_population_evolution(self, ax, selected_species=None):
        """
        Vẽ biểu đồ tiến hóa dân số của các loài
        """
        ax.set_title('Tiến hóa dân số các loài', fontsize=12, fontweight='bold')
        ax.set_xlabel('Thời gian (bước)')
        ax.set_ylabel('Dân số (log scale)')
        ax.set_yscale('log')
        
        # Chọn loài để vẽ
        if selected_species is None:
            # Chọn 10 loài đại diện với các đặc điểm khác nhau
            selected_species = self._select_representative_species(10)
        
        # Vẽ đường dân số cho mỗi loài
        colors = plt.cm.tab20(np.linspace(0, 1, len(selected_species)))
        
        for idx, species_id in enumerate(selected_species):
            species = self.species_list[species_id]
            pop_history = np.array(species.population_history)
            
            # Thay thế 0 bằng NaN để không vẽ trên log scale
            pop_history[pop_history <= 0] = np.nan
            
            ax.plot(range(len(pop_history)), pop_history,
                   label=species.name, color=colors[idx],
                   linewidth=1.5, alpha=0.8)
        
        ax.grid(True, alpha=0.3)
        ax.legend(loc='upper right', ncol=2, fontsize=8)
    
    def _plot_state_heatmap(self, ax):
        """
        Vẽ heatmap trạng thái của các loài theo thời gian
        """
        ax.set_title('Heatmap trạng thái tiến hóa', fontsize=12, fontweight='bold')
        ax.set_xlabel('Thời gian (bước)')
        ax.set_ylabel('Loài')
        
        # Tạo ma trận trạng thái
        state_matrix = np.array([s.state_history for s in self.species_list])
        
        # Tạo colormap tùy chỉnh cho 4 trạng thái
        colors_map = ['#2ecc71', '#f39c12', '#e74c3c', '#34495e']  # Xanh, Vàng, Đỏ, Xám
        n_bins = 4
        cmap = mcolors.ListedColormap(colors_map)
        bounds = [0, 1, 2, 3, 4]
        norm = mcolors.BoundaryNorm(bounds, cmap.N)
        
        # Vẽ heatmap
        im = ax.imshow(state_matrix, cmap=cmap, norm=norm, 
                      aspect='auto', interpolation='nearest')
        
        # Thêm colorbar
        cbar = plt.colorbar(im, ax=ax, ticks=[0.5, 1.5, 2.5, 3.5])
        cbar.set_ticklabels(['Khởi tạo', 'Phát triển', 'Suy tàn', 'Tuyệt chủng'])
        
        # Giảm số lượng tick trên trục y nếu có nhiều loài
        if self.n_species > 20:
            ax.set_yticks(np.arange(0, self.n_species, 5))
            ax.set_yticklabels([f"L{i+1}" for i in range(0, self.n_species, 5)])
    
    def _plot_state_distribution(self, ax):
        """
        Vẽ biểu đồ phân bố trạng thái theo thời gian
        """
        ax.set_title('Phân bố trạng thái theo thời gian', fontsize=12, fontweight='bold')
        ax.set_xlabel('Thời gian (bước)')
        ax.set_ylabel('Số lượng loài')
        
        # Đếm số loài ở mỗi trạng thái theo thời gian
        time_points = range(self.time_steps + 1)
        state_counts = {state: [] for state in EvolutionState}
        
        for t in time_points:
            counts = {state: 0 for state in EvolutionState}
            for species in self.species_list:
                if t < len(species.state_history):
                    state = EvolutionState(species.state_history[t])
                    counts[state] += 1
            
            for state in EvolutionState:
                state_counts[state].append(counts[state])
        
        # Vẽ biểu đồ xếp chồng
        colors_map = ['#2ecc71', '#f39c12', '#e74c3c', '#34495e']
        labels = ['Khởi tạo', 'Phát triển', 'Suy tàn', 'Tuyệt chủng']
        
        bottom = np.zeros(len(time_points))
        for idx, state in enumerate(EvolutionState):
            ax.fill_between(time_points, bottom, bottom + state_counts[state],
                          label=labels[idx], color=colors_map[idx], alpha=0.8)
            bottom += state_counts[state]
        
        ax.legend(loc='upper right')
        ax.grid(True, alpha=0.3)
    
    def _plot_ecosystem_metrics_vi(self, ax):
        """
        Vẽ các chỉ số tổng quan của hệ sinh thái
        """
        ax.set_title('Chỉ số hệ sinh thái', fontsize=12, fontweight='bold')
        ax.set_xlabel('Thời gian (bước)')
        
        time_points = range(self.time_steps + 1)
        
        # Tính tổng dân số và số loài còn sống
        total_population = []
        alive_species = []
        shannon_diversity = []  # Chỉ số đa dạng Shannon
        
        for t in time_points:
            total_pop = 0
            alive_count = 0
            populations = []
            
            for species in self.species_list:
                if t < len(species.population_history):
                    pop = species.population_history[t]
                    total_pop += pop
                    if pop > 0:
                        alive_count += 1
                        populations.append(pop)
            
            total_population.append(total_pop)
            alive_species.append(alive_count)
            
            # Tính chỉ số Shannon diversity
            if total_pop > 0:
                proportions = np.array(populations) / total_pop
                shannon = -np.sum(proportions * np.log(proportions + 1e-10))
                shannon_diversity.append(shannon)
            else:
                shannon_diversity.append(0)
        
        # Vẽ với 2 trục y
        color1 = 'tab:blue'
        ax.plot(time_points, total_population, color=color1, 
               label='Tổng dân số', linewidth=2)
        ax.set_ylabel('Tổng dân số', color=color1)
        ax.tick_params(axis='y', labelcolor=color1)
        
        ax2 = ax.twinx()
        color2 = 'tab:orange'
        ax2.plot(time_points, alive_species, color=color2, 
                label='Số loài sống', linewidth=2, linestyle='--')
        ax2.set_ylabel('Số loài còn sống', color=color2)
        ax2.tick_params(axis='y', labelcolor=color2)
        
        # Thêm chỉ số đa dạng Shannon (chuẩn hóa)
        ax3 = ax.twinx()
        ax3.spines['right'].set_position(('outward', 60))
        color3 = 'tab:green'
        shannon_normalized = np.array(shannon_diversity) / np.max(shannon_diversity + [1])
        ax3.plot(time_points, shannon_normalized, color=color3,
                label='Đa dạng Shannon', linewidth=1, alpha=0.6)
        ax3.set_ylabel('Chỉ số đa dạng Shannon', color=color3)
        ax3.tick_params(axis='y', labelcolor=color3)
        
        ax.grid(True, alpha=0.3)
        
        # Kết hợp legends
        lines1, labels1 = ax.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        lines3, labels3 = ax3.get_legend_handles_labels()
        ax.legend(lines1 + lines2 + lines3, labels1 + labels2 + labels3, 
                 loc='upper right')
    # _plot_ecosystem_metrics with English titles
    def _plot_ecosystem_metrics_en(self, ax):
        """
        Vẽ các chỉ số tổng quan của hệ sinh thái
        """
        ax.set_title('Ecosystem Metrics', fontsize=12, fontweight='bold')
        ax.set_xlabel('Time Steps')
        
        time_points = range(self.time_steps + 1)
        
        # Tính tổng dân số và số loài còn sống
        total_population = []
        alive_species = []
        shannon_diversity = []  # Chỉ số đa dạng Shannon
        
        for t in time_points:
            total_pop = 0
            alive_count = 0
            populations = []
            
            for species in self.species_list:
                if t < len(species.population_history):
                    pop = species.population_history[t]
                    total_pop += pop
                    if pop > 0:
                        alive_count += 1
                        populations.append(pop)
            
            total_population.append(total_pop)
            alive_species.append(alive_count)
            
            # Tính chỉ số Shannon diversity
            if total_pop > 0:
                proportions = np.array(populations) / total_pop
                shannon = -np.sum(proportions * np.log(proportions + 1e-10))
                shannon_diversity.append(shannon)
            else:
                shannon_diversity.append(0)
        
        # Vẽ với 2 trục y
        color1 = 'tab:blue'
        ax.plot(time_points, total_population, color=color1, 
               label='Total Population', linewidth=2)
        ax.set_ylabel('Total Population', color=color1)
        ax.tick_params(axis='y', labelcolor=color1)
        
        ax2 = ax.twinx()
        color2 = 'tab:orange'
        ax2.plot(time_points, alive_species, color=color2, 
                label='Alive Species Count', linewidth=2, linestyle='--')
        ax2.set_ylabel('Alive Species Count', color=color2)
        ax2.tick_params(axis='y', labelcolor=color2)
        
        # Thêm chỉ số đa dạng Shannon (chuẩn hóa)
        ax3 = ax.twinx()
        ax3.spines['right'].set_position(('outward', 60))
        color3 = 'tab:green'
        shannon_normalized = np.array(shannon_diversity) / np.max(shannon_diversity + [1])
        ax3.plot(time_points, shannon_normalized, color=color3,
                label='Shannon Diversity', linewidth=1, alpha=0.6)
        ax3.set_ylabel('Shannon Diversity Index', color=color3)
        ax3.tick_params(axis='y', labelcolor=color3)
        ax.grid(True, alpha=0.3)
        # Kết hợp legends
         # Kết hợp legends
        lines1, labels1 = ax.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        lines3, labels3 = ax3.get_legend_handles_labels()
        ax.legend(lines1 + lines2 + lines3, labels1 + labels2 + labels3, 
                 loc='upper right')
    def _select_representative_species(self, n=10):
        """
        Chọn các loài đại diện để hiển thị
        
        Parameters:
        -----------
        n : int
            Số loài cần chọn
            
        Returns:
        --------
        List[int] : Danh sách ID các loài được chọn
        """
        selected = []
        
        # Chọn loài theo các tiêu chí khác nhau
        # 1. Loài có dân số cao nhất
        max_pop_species = max(range(self.n_species), 
                             key=lambda i: max(self.species_list[i].population_history))
        selected.append(max_pop_species)
        
        # 2. Loài tuyệt chủng sớm nhất
        extinct_species = [i for i in range(self.n_species) 
                          if self.species_list[i].state == EvolutionState.EXTINCT]
        if extinct_species:
            selected.append(extinct_species[0])
        
        # 3. Loài còn sống với các trạng thái khác nhau
        for state in [EvolutionState.GROWTH, EvolutionState.DECAY, EvolutionState.INIT]:
            species_in_state = [i for i in range(self.n_species) 
                               if self.species_list[i].state == state]
            if species_in_state:
                selected.append(species_in_state[0])
        
        # 4. Chọn ngẫu nhiên cho đủ số lượng
        remaining = n - len(selected)
        if remaining > 0:
            available = [i for i in range(self.n_species) if i not in selected]
            selected.extend(np.random.choice(available, 
                                           min(remaining, len(available)), 
                                           replace=False))
        
        return selected[:n]
    
    def create_animation(self, filename='ecosystem_animation.mp4'):
        """
        Tạo animation cho quá trình mô phỏng
        
        Parameters:
        -----------
        filename : str
            Tên file để lưu animation
        """
        print(f"\n🎬 Đang tạo animation '{filename}'...")
        
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
        fig.suptitle('Mô phỏng Hệ sinh thái - Animation', fontsize=14)
        
        # Chọn 5 loài để theo dõi
        selected_species = self._select_representative_species(5)
        
        def animate(frame):
            ax1.clear()
            ax2.clear()
            
            # Plot 1: Dân số các loài
            ax1.set_title(f'Dân số các loài (t={frame})')
            ax1.set_xlabel('Loài')
            ax1.set_ylabel('Dân số')
            ax1.set_yscale('log')
            
            populations = []
            labels = []
            colors = []
            
            for species_id in selected_species:
                species = self.species_list[species_id]
                if frame < len(species.population_history):
                    pop = species.population_history[frame]
                    populations.append(max(pop, 0.1))  # Tránh log(0)
                    labels.append(species.name)
                    
                    # Màu theo trạng thái
                    state = EvolutionState(species.state_history[frame])
                    color_map = {
                        EvolutionState.INIT: '#2ecc71',
                        EvolutionState.GROWTH: '#f39c12',
                        EvolutionState.DECAY: '#e74c3c',
                        EvolutionState.EXTINCT: '#34495e'
                    }
                    colors.append(color_map[state])
            
            ax1.bar(labels, populations, color=colors)
            ax1.set_ylim([0.1, max(populations) * 2 if populations else 1000])
            
            # Plot 2: Lịch sử dân số
            ax2.set_title('Lịch sử dân số')
            ax2.set_xlabel('Thời gian')
            ax2.set_ylabel('Dân số (log scale)')
            ax2.set_yscale('log')
            
            for species_id in selected_species:
                species = self.species_list[species_id]
                history = species.population_history[:frame+1]
                history = [max(h, 0.1) for h in history]  # Tránh log(0)
                ax2.plot(range(len(history)), history, 
                        label=species.name, linewidth=2)
            
            ax2.legend(loc='upper right', fontsize=8)
            ax2.grid(True, alpha=0.3)
            
            return ax1, ax2
        
        # Tạo animation
        anim = FuncAnimation(fig, animate, frames=self.time_steps,
                           interval=50, blit=False)
        
        # Lưu animation
        try:
            anim.save(filename, writer='pillow', fps=20)
            print(f"✅ Animation đã được lưu: {filename}")
        except:
            print("⚠️  Không thể lưu animation. Hiển thị trực tiếp...")
            plt.show()

def main():
    """
    Hàm chính để chạy mô phỏng
    """
    print("=" * 60)
    print(" HỆ SINH THÁI KHÉP KÍN - MÔ PHỎNG SINH TRỤ HOẠI DIỆT")
    print("=" * 60)
    
    # Tạo và chạy mô phỏng
    ecosystem = EcosystemSimulation(
        n_species=30,      # Số lượng loài
        time_steps=3000,    # Số bước thời gian
        seed=42           # Seed để có thể tái tạo kết quả
    )
    
    # Chạy mô phỏng
    ecosystem.run_simulation()
    
    # In các đặc trưng của loài không bị tuyệt chủng (từ dữ liệu mô phỏng)
    ecosystem.print_non_extinct_species_features()

    # Vẽ kết quả (tùy chọn — có thể tắt nếu chỉ cần kiểm tra nhanh)
    print("\n📈 Đang tạo biểu đồ...")
    ecosystem.plot_results()
    
    # Tùy chọn: Tạo animation
    # ecosystem.create_animation()
    
    return ecosystem

if __name__ == "__main__":
    ecosystem = main()