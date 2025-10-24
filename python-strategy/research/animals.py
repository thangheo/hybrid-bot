"""
Mô phỏng Hệ sinh thái khép kín với Tương tác Loài - Lotka-Volterra
===================================================================
Chương trình mô phỏng sự phát triển và suy tàn của nhiều loài
trong một hệ sinh thái khép kín theo thời gian, có xét đến
tương tác cạnh tranh, săn mồi, và cộng sinh giữa các loài.

Author: Claude
Date: 2025
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.animation import FuncAnimation
import seaborn as sns
from dataclasses import dataclass, field
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
    population_history: List[float] = field(default_factory=list)
    state_history: List[int] = field(default_factory=list)
    
    def __post_init__(self):
        """Khởi tạo các thuộc tính lịch sử"""
        self.population_history = [self.population]
        self.state_history = [self.state.value]
        self.state_duration = 0

class InteractionType(Enum):
    """Định nghĩa các loại tương tác giữa các loài"""
    NONE = 0           # Không tương tác
    COMPETITION = 1    # Cạnh tranh (- , -)
    PREDATION = 2      # Săn mồi (+ , -)
    MUTUALISM = 3      # Cộng sinh (+ , +)
    PARASITISM = 4     # Ký sinh (+ , -)

class EcosystemSimulation:
    """
    Lớp chính để mô phỏng hệ sinh thái khép kín với tương tác loài
    """
    
    def __init__(self, n_species=30, time_steps=500, seed=42, 
                 interaction_strength=0.3, interaction_density=0.15):
        """
        Khởi tạo hệ sinh thái với ma trận tương tác
        
        Parameters:
        -----------
        n_species : int
            Số lượng loài trong hệ sinh thái
        time_steps : int
            Số bước thời gian mô phỏng
        seed : int
            Seed cho random number generator
        interaction_strength : float
            Độ mạnh trung bình của tương tác (0-1)
        interaction_density : float
            Tỷ lệ loài có tương tác với nhau (0-1)
        """
        np.random.seed(seed)
        self.n_species = n_species
        self.time_steps = time_steps
        self.current_time = 0
        
        # Tham số mô phỏng
        self.noise_level = 0.05
        self.interaction_strength = interaction_strength
        self.interaction_density = interaction_density
        
        # Ngưỡng chuyển trạng thái
        self.thresholds = {
            'init_to_growth': 0.2,
            'growth_to_decay': 0.8,
            'decay_to_extinct': 0.05,
            'min_population': 1.0
        }
        
        # Khởi tạo các loài
        self.species_list = self._initialize_species()
        
        # Khởi tạo ma trận tương tác
        self.alpha_matrix = self._initialize_interaction_matrix()
        
        # Lưu lịch sử tương tác
        self.interaction_history = []
    
    def _initialize_species(self) -> List[Species]:
        """
        Khởi tạo danh sách các loài với tham số ngẫu nhiên
        """
        species_list = []
        
        for i in range(self.n_species):
            # Tạo tham số ngẫu nhiên theo phân phối lognormal
            initial_pop = np.random.lognormal(3, 1) * 10
            carrying_capacity = np.random.lognormal(5, 0.5) * 100
            
            # Tỷ lệ sinh/tử ngẫu nhiên
            birth_rate = np.random.uniform(0.1, 0.5)
            death_rate = np.random.uniform(0.05, 0.3)
            
            # Xác định trạng thái ban đầu
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
                state_duration=0
            )
            
            species_list.append(species)
        
        return species_list
    
    def _initialize_interaction_matrix(self) -> np.ndarray:
        """
        Khởi tạo ma trận tương tác Alpha (n_species x n_species)
        
        Alpha[i][j] = Hệ số ảnh hưởng của loài j lên loài i
        
        Quy tắc:
        - Dương: tốt cho loài i
        - Âm: xấu cho loài i
        - 0: không ảnh hưởng
        
        Returns:
        --------
        np.ndarray: Ma trận tương tác (n_species x n_species)
        """
        alpha = np.zeros((self.n_species, self.n_species))
        
        # Đường chéo chính: tự cạnh tranh với chính mình (luôn âm)
        for i in range(self.n_species):
            alpha[i][i] = -1.0  # Loài i bị ảnh hưởng xấu bởi chính nó
        
        # Tạo tương tác giữa các loài khác nhau
        for i in range(self.n_species):
            for j in range(i + 1, self.n_species):
                # Quyết định xem có tương tác không
                if np.random.random() < self.interaction_density:
                    # Chọn loại tương tác ngẫu nhiên
                    interaction_type = np.random.choice([
                        InteractionType.COMPETITION,
                        InteractionType.PREDATION,
                        InteractionType.MUTUALISM,
                        InteractionType.PARASITISM
                    ])
                    
                    # Cạnh tranh: cả hai đều bị hại (alpha < 0)
                    if interaction_type == InteractionType.COMPETITION:
                        alpha_ij = -np.random.uniform(0.1, self.interaction_strength)
                        alpha_ji = -np.random.uniform(0.1, self.interaction_strength)
                    
                    # Săn mồi: i lợi (alpha > 0), j bị hại (alpha < 0)
                    elif interaction_type == InteractionType.PREDATION:
                        if np.random.random() < 0.5:
                            # i là kẻ săn mồi
                            alpha_ij = np.random.uniform(0.05, self.interaction_strength)
                            alpha_ji = -np.random.uniform(0.1, self.interaction_strength)
                        else:
                            # j là kẻ săn mồi
                            alpha_ij = -np.random.uniform(0.1, self.interaction_strength)
                            alpha_ji = np.random.uniform(0.05, self.interaction_strength)
                    
                    # Cộng sinh: cả hai đều lợi (alpha > 0)
                    elif interaction_type == InteractionType.MUTUALISM:
                        alpha_ij = np.random.uniform(0.05, self.interaction_strength * 0.5)
                        alpha_ji = np.random.uniform(0.05, self.interaction_strength * 0.5)
                    
                    # Ký sinh: i lợi, j bị hại (giống săn mồi nhưng mức độ khác)
                    else:  # PARASITISM
                        alpha_ij = np.random.uniform(0.01, self.interaction_strength * 0.3)
                        alpha_ji = -np.random.uniform(0.05, self.interaction_strength * 0.3)
                    
                    alpha[i][j] = alpha_ij
                    alpha[j][i] = alpha_ji
        
        return alpha
    
    def _calculate_growth_rate(self, species: Species) -> float:
        """
        Tính tốc độ tăng trưởng dựa trên trạng thái hiện tại
        """
        base_r = species.birth_rate - species.death_rate
        
        if species.state == EvolutionState.INIT:
            return base_r * 0.8
        elif species.state == EvolutionState.GROWTH:
            return base_r * 1.5
        elif species.state == EvolutionState.DECAY:
            return -abs(base_r) * 0.5
        else:
            return 0
    
    def _update_state(self, species: Species):
        """
        Cập nhật trạng thái của loài dựa trên ngưỡng dân số
        """
        if species.state == EvolutionState.EXTINCT:
            return
        
        pop_ratio = species.population / species.carrying_capacity
        species.state_duration += 1
        
        if species.state == EvolutionState.INIT:
            if pop_ratio > self.thresholds['init_to_growth']:
                species.state = EvolutionState.GROWTH
                species.state_duration = 0
        
        elif species.state == EvolutionState.GROWTH:
            if pop_ratio > self.thresholds['growth_to_decay']:
                species.state = EvolutionState.DECAY
                species.state_duration = 0
            elif pop_ratio < self.thresholds['init_to_growth'] * 0.5:
                species.state = EvolutionState.INIT
                species.state_duration = 0
        
        elif species.state == EvolutionState.DECAY:
            if pop_ratio < self.thresholds['decay_to_extinct']:
                species.state = EvolutionState.EXTINCT
                species.state_duration = 0
            elif pop_ratio < 0.4 and species.state_duration > 20:
                if np.random.random() < 0.1:
                    species.state = EvolutionState.INIT
                    species.state_duration = 0
        
        if species.population < self.thresholds['min_population']:
            species.state = EvolutionState.EXTINCT
            species.population = 0
    
    def _update_population_with_interactions(self, species: Species, species_list: List[Species]):
        """
        Cập nhật dân số theo mô hình Lotka-Volterra tổng quát với tương tác
        
        Phương trình:
        dN_i/dt = r_i * N_i * ((K_i - N_i - Σ(α_ij * N_j)) / K_i)
        
        Parameters:
        -----------
        species : Species
            Loài cần cập nhật
        species_list : List[Species]
            Danh sách tất cả các loài (để tính tương tác)
        """
        if species.state == EvolutionState.EXTINCT:
            species.population = 0
            return
        
        r = self._calculate_growth_rate(species)
        K = species.carrying_capacity
        N = species.population
        
        # BƯỚC 1: Tính tổng tương tác từ tất cả các loài khác
        interaction_sum = 0
        for j in range(self.n_species):
            if j == species.id:
                # Loài tự cạnh tranh với chính mình
                interaction_sum += self.alpha_matrix[species.id][j] * N
            else:
                # Ảnh hưởng của loài j lên loài i
                interaction_sum += self.alpha_matrix[species.id][j] * species_list[j].population
        
        # BƯỚC 2: Tính sức chứa hiệu quả (effective carrying capacity)
        # effective_K = K - interaction_sum
        effective_K = K - interaction_sum
        
        # BƯỚC 3: Tính tốc độ thay đổi (dN/dt)
        if effective_K > 0:
            # Trường hợp bình thường
            change_rate = r * (effective_K / K)
        else:
            # Nếu bị quá tải, tốc độ tăng trưởng bị âm (suy tàn)
            change_rate = -abs(r)
        
        # BƯỚC 4: Tính dân số mới với nhiễu ngẫu nhiên
        delta_population = N * change_rate
        noise = np.random.normal(0, self.noise_level * N)
        N_new = N + delta_population + noise
        
        # BƯỚC 5: Kiểm tra điều kiện
        species.population = max(N_new, 0)
        
        if species.population < self.thresholds['min_population']:
            species.population = 0
    
    def run_simulation(self):
        """
        Chạy mô phỏng hệ sinh thái
        """
        print(f"\n🌿 Bắt đầu mô phỏng hệ sinh thái với {self.n_species} loài...")
        print(f"   Số bước thời gian: {self.time_steps}")
        print(f"   Ma trận tương tác: {self.n_species}x{self.n_species}")
        print(f"   Mật độ tương tác: {self.interaction_density:.1%}")
        print(f"   Độ mạnh tương tác: {self.interaction_strength:.2f}")
        
        for t in range(self.time_steps):
            # Cập nhật dân số của tất cả các loài đồng thời
            # (điều quan trọng: không cập nhật tuần tự)
            for species in self.species_list:
                self._update_population_with_interactions(species, self.species_list)
            
            # Cập nhật trạng thái sau khi tất cả dân số đã được cập nhật
            for species in self.species_list:
                self._update_state(species)
                species.population_history.append(species.population)
                species.state_history.append(species.state.value)
            
            # In tiến độ
            if (t + 1) % (self.time_steps // 10) == 0:
                alive_count = sum(1 for s in self.species_list if s.population > 0)
                total_pop = sum(s.population for s in self.species_list)
                print(f"   ⏱️  Bước {t+1}/{self.time_steps} - Loài còn sống: {alive_count}/{self.n_species}, " 
                      f"Tổng dân số: {total_pop:.0f}")
        
        self.current_time = self.time_steps
        print("✅ Mô phỏng hoàn thành!")
    
    def print_non_extinct_species_features(self):
        """
        In các đặc trưng của loài không bị tuyệt chủng
        """
        print("\n" + "="*80)
        print("📊 DANH SÁCH CÁC LOÀI KHÔNG BỊ TUYỆT CHỦNG")
        print("="*80)
        
        non_extinct = [s for s in self.species_list if s.state != EvolutionState.EXTINCT]
        
        print(f"\n✨ Số loài còn sống: {len(non_extinct)}/{self.n_species}")
        print(f"💀 Số loài bị tuyệt chủng: {self.n_species - len(non_extinct)}")
        print(f"📈 Tổng dân số cuối cùng: {sum(s.population for s in non_extinct):.0f}\n")
        
        # Sắp xếp theo dân số giảm dần
        non_extinct_sorted = sorted(non_extinct, 
                                   key=lambda x: x.population, 
                                   reverse=True)
        
        print(f"{'Loài':<12} {'Dân số hiện tại':<20} {'Sức chứa':<15} {'Trạng thái':<12} {'Tốc độ Sinh':<12} {'Tốc độ Tử':<12}")
        print("-" * 85)
        
        for species in non_extinct_sorted[:20]:  # In top 20
            state_name = species.state.name
            print(f"{species.name:<12} {species.population:>15.0f}     "
                  f"{species.carrying_capacity:>12.0f}     {state_name:<12} "
                  f"{species.birth_rate:>10.3f}    {species.death_rate:>10.3f}")
    
    def analyze_interactions(self):
        """
        Phân tích ma trận tương tác và các loại tương tác có trong hệ sinh thái
        """
        print("\n" + "="*80)
        print("🔗 PHÂN TÍCH MA TRẬN TƯƠNG TÁC")
        print("="*80)
        
        # Đếm các loại tương tác
        competition_count = 0
        predation_count = 0
        mutualism_count = 0
        parasitism_count = 0
        none_count = 0
        
        for i in range(self.n_species):
            for j in range(i + 1, self.n_species):
                alpha_ij = self.alpha_matrix[i][j]
                alpha_ji = self.alpha_matrix[j][i]
                
                # Bỏ qua đường chéo chính
                if abs(alpha_ij) < 1e-6 and abs(alpha_ji) < 1e-6:
                    none_count += 1
                elif alpha_ij < 0 and alpha_ji < 0:
                    competition_count += 1
                elif (alpha_ij > 0 and alpha_ji < 0) or (alpha_ij < 0 and alpha_ji > 0):
                    predation_count += 1
                elif alpha_ij > 0 and alpha_ji > 0:
                    mutualism_count += 1
                else:
                    parasitism_count += 1
        
        total_pairs = self.n_species * (self.n_species - 1) // 2
        
        print(f"\n📈 Thống kê tương tác (trong {total_pairs} cặp loài):")
        print(f"   • Cạnh tranh (Competition):     {competition_count:>4} ({competition_count/total_pairs*100:>5.1f}%)")
        print(f"   • Săn mồi (Predation):          {predation_count:>4} ({predation_count/total_pairs*100:>5.1f}%)")
        print(f"   • Cộng sinh (Mutualism):        {mutualism_count:>4} ({mutualism_count/total_pairs*100:>5.1f}%)")
        print(f"   • Ký sinh (Parasitism):         {parasitism_count:>4} ({parasitism_count/total_pairs*100:>5.1f}%)")
        print(f"   • Không tương tác (None):       {none_count:>4} ({none_count/total_pairs*100:>5.1f}%)")
        
        # Tìm các mối quan hệ mạnh nhất
        print(f"\n🔴 Những tương tác mạnh nhất (|α| > 0.5):")
        strong_interactions = []
        for i in range(self.n_species):
            for j in range(i + 1, self.n_species):
                alpha_ij = self.alpha_matrix[i][j]
                alpha_ji = self.alpha_matrix[j][i]
                
                if abs(alpha_ij) > 0.5 or abs(alpha_ji) > 0.5:
                    strong_interactions.append((i, j, alpha_ij, alpha_ji))
        
        strong_interactions.sort(key=lambda x: max(abs(x[2]), abs(x[3])), reverse=True)
        
        for i, j, alpha_ij, alpha_ji in strong_interactions[:10]:
            species_i = self.species_list[i]
            species_j = self.species_list[j]
            
            # Xác định loại tương tác
            if alpha_ij < 0 and alpha_ji < 0:
                interaction_type = "Cạnh tranh"
            elif alpha_ij > 0 and alpha_ji < 0:
                interaction_type = f"{species_i.name} săn mồi {species_j.name}"
            elif alpha_ij < 0 and alpha_ji > 0:
                interaction_type = f"{species_j.name} săn mồi {species_i.name}"
            elif alpha_ij > 0 and alpha_ji > 0:
                interaction_type = "Cộng sinh"
            else:
                interaction_type = "Ký sinh"
            
            print(f"   • {species_i.name} ↔ {species_j.name}: α_ij={alpha_ij:>6.3f}, α_ji={alpha_ji:>6.3f} ({interaction_type})")
    
    def plot_results(self):
        """
        Vẽ kết quả mô phỏng
        """
        fig = plt.figure(figsize=(16, 10))
        gs = fig.add_gridspec(2, 2, hspace=0.3, wspace=0.3)
        
        # Plot 1: Dân số của 10 loài được chọn
        ax1 = fig.add_subplot(gs[0, 0])
        self._plot_population_traces(ax1)
        
        # Plot 2: Các chỉ số hệ sinh thái
        ax2 = fig.add_subplot(gs[0, 1])
        self._plot_ecosystem_metrics(ax2)
        
        # Plot 3: Ma trận tương tác
        ax3 = fig.add_subplot(gs[1, 0])
        self._plot_interaction_matrix(ax3)
        
        # Plot 4: Trạng thái cuối cùng
        ax4 = fig.add_subplot(gs[1, 1])
        self._plot_final_state(ax4)
        
        plt.suptitle('Mô phỏng Hệ sinh thái với Tương tác Loài (Lotka-Volterra)', 
                    fontsize=16, fontweight='bold', y=0.995)
        plt.savefig('ecosystem_with_interactions.png', dpi=150, bbox_inches='tight')
        print("✅ Biểu đồ đã được lưu: ecosystem_with_interactions.png")
        plt.show()
    
    def _plot_population_traces(self, ax):
        """
        Vẽ lịch sử dân số của các loài được chọn
        """
        ax.set_title('Lịch sử Dân số của Các Loài', fontsize=12, fontweight='bold')
        ax.set_xlabel('Bước thời gian')
        ax.set_ylabel('Dân số (log scale)')
        ax.set_yscale('log')
        
        selected_species = self._select_representative_species(10)
        
        for species_id in selected_species:
            species = self.species_list[species_id]
            history = [max(h, 0.1) for h in species.population_history]
            ax.plot(range(len(history)), history, 
                   label=species.name, linewidth=1.5, alpha=0.8)
        
        ax.legend(loc='upper right', fontsize=8, ncol=2)
        ax.grid(True, alpha=0.3)
    
    def _plot_ecosystem_metrics(self, ax):
        """
        Vẽ các chỉ số của hệ sinh thái
        """
        ax.set_title('Chỉ số Hệ sinh thái', fontsize=12, fontweight='bold')
        ax.set_xlabel('Bước thời gian')
        
        time_points = range(self.time_steps + 1)
        
        # Tính tổng dân số và số loài còn sống
        total_population = []
        alive_species = []
        shannon_diversity = []
        
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
            
            if total_pop > 0:
                proportions = np.array(populations) / total_pop
                shannon = -np.sum(proportions * np.log(proportions + 1e-10))
                shannon_diversity.append(shannon)
            else:
                shannon_diversity.append(0)
        
        # Vẽ với trục Y kép
        color1 = 'tab:blue'
        ax.plot(time_points, total_population, color=color1, 
               label='Tổng dân số', linewidth=2)
        ax.set_ylabel('Tổng dân số', color=color1)
        ax.tick_params(axis='y', labelcolor=color1)
        
        ax2 = ax.twinx()
        color2 = 'tab:orange'
        ax2.plot(time_points, alive_species, color=color2, 
                label='Số loài còn sống', linewidth=2, linestyle='--')
        ax2.set_ylabel('Số loài còn sống', color=color2)
        ax2.tick_params(axis='y', labelcolor=color2)
        
        ax3 = ax.twinx()
        ax3.spines['right'].set_position(('outward', 60))
        color3 = 'tab:green'
        max_shannon = max(shannon_diversity) if shannon_diversity else 1
        shannon_normalized = np.array(shannon_diversity) / max(max_shannon, 1)
        ax3.plot(time_points, shannon_normalized, color=color3,
                label='Shannon Diversity', linewidth=1.5, alpha=0.6, linestyle=':')
        ax3.set_ylabel('Shannon Diversity Index', color=color3)
        ax3.tick_params(axis='y', labelcolor=color3)
        
        ax.grid(True, alpha=0.3)
        lines1, labels1 = ax.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        lines3, labels3 = ax3.get_legend_handles_labels()
        ax.legend(lines1 + lines2 + lines3, labels1 + labels2 + labels3, 
                 loc='upper left', fontsize=8)
    
    def _plot_interaction_matrix(self, ax):
        """
        Vẽ ma trận tương tác dưới dạng heatmap
        """
        # Sắp xếp ma trận tương tác
        im = ax.imshow(self.alpha_matrix, cmap='RdBu_r', aspect='auto', 
                      vmin=-1, vmax=1, interpolation='nearest')
        ax.set_title('Ma trận Tương tác (Lotka-Volterra)', fontsize=12, fontweight='bold')
        ax.set_xlabel('Loài j')
        ax.set_ylabel('Loài i')
        
        # Thêm colorbar
        cbar = plt.colorbar(im, ax=ax)
        cbar.set_label('Hệ số tương tác (α_ij)', rotation=270, labelpad=20)
        
        # Chỉ hiển thị một số tick để tránh quá tải
        tick_spacing = max(1, self.n_species // 10)
        ax.set_xticks(range(0, self.n_species, tick_spacing))
        ax.set_yticks(range(0, self.n_species, tick_spacing))
    
    def _plot_final_state(self, ax):
        """
        Vẽ dân số cuối cùng của các loài
        """
        # Lấy dân số cuối cùng
        final_populations = [s.population for s in self.species_list]
        final_states = [s.state for s in self.species_list]
        
        # Sắp xếp theo dân số
        sorted_indices = np.argsort(final_populations)[::-1]
        sorted_populations = [final_populations[i] for i in sorted_indices]
        sorted_names = [f"Loài {i+1:02d}" for i in sorted_indices]
        sorted_states = [final_states[i] for i in sorted_indices]
        
        # Tạo colormap dựa trên trạng thái
        color_map = {
            EvolutionState.INIT: '#2ecc71',
            EvolutionState.GROWTH: '#f39c12',
            EvolutionState.DECAY: '#e74c3c',
            EvolutionState.EXTINCT: '#95a5a6'
        }
        colors = [color_map[state] for state in sorted_states]
        
        # Vẽ bar chart
        bars = ax.barh(range(len(sorted_populations)), sorted_populations, color=colors)
        ax.set_yticks(range(len(sorted_populations)))
        ax.set_yticklabels(sorted_names, fontsize=8)
        ax.set_xlabel('Dân số cuối cùng')
        ax.set_title('Dân số Cuối cùng của Các Loài', fontsize=12, fontweight='bold')
        ax.set_xscale('log')
        ax.grid(True, alpha=0.3, axis='x')
        
        # Thêm legend
        from matplotlib.patches import Patch
        legend_elements = [
            Patch(facecolor='#2ecc71', label='INIT'),
            Patch(facecolor='#f39c12', label='GROWTH'),
            Patch(facecolor='#e74c3c', label='DECAY'),
            Patch(facecolor='#95a5a6', label='EXTINCT')
        ]
        ax.legend(handles=legend_elements, loc='lower right', fontsize=8)
    
    def _select_representative_species(self, n=10):
        """
        Chọn các loài đại diện để hiển thị
        """
        selected = []
        
        # Chọn loài có dân số cao nhất
        max_pop_species = max(range(self.n_species), 
                             key=lambda i: max(self.species_list[i].population_history) if self.species_list[i].population_history else 0)
        selected.append(max_pop_species)
        
        # Chọn loài tuyệt chủng sớm nhất
        extinct_species = [i for i in range(self.n_species) 
                          if self.species_list[i].state == EvolutionState.EXTINCT]
        if extinct_species:
            selected.append(extinct_species[0])
        
        # Chọn loài theo trạng thái
        for state in [EvolutionState.GROWTH, EvolutionState.DECAY, EvolutionState.INIT]:
            species_in_state = [i for i in range(self.n_species) 
                               if self.species_list[i].state == state]
            if species_in_state:
                selected.append(species_in_state[0])
        
        # Chọn ngẫu nhiên
        remaining = n - len(selected)
        if remaining > 0:
            available = [i for i in range(self.n_species) if i not in selected]
            selected.extend(np.random.choice(available, 
                                           min(remaining, len(available)), 
                                           replace=False))
        
        return selected[:n]


def main():
    """
    Hàm chính để chạy mô phỏng
    """
    print("=" * 80)
    print(" HỆ SINH THÁI KHÉP KÍN - MÔ PHỎNG VỚI TƯƠNG TÁC LOÀI (LOTKA-VOLTERRA)")
    print("=" * 80)
    
    # Tạo và chạy mô phỏng
    ecosystem = EcosystemSimulation(
        n_species=30,
        time_steps=500,
        seed=42,
        interaction_strength=0.4,
        interaction_density=0.20
    )
    
    # Chạy mô phỏng
    ecosystem.run_simulation()
    
    # In các đặc trưng của loài không bị tuyệt chủng
    ecosystem.print_non_extinct_species_features()
    
    # Phân tích ma trận tương tác
    ecosystem.analyze_interactions()
    
    # Vẽ kết quả
    print("\n📈 Đang tạo biểu đồ...")
    ecosystem.plot_results()
    
    return ecosystem


if __name__ == "__main__":
    ecosystem = main()